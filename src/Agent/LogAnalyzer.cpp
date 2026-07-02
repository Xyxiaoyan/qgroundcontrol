#include "LogAnalyzer.h"

#include <Utilities/Parsing/ULog/ULogUtility.h>
#include <Utilities/Parsing/DataFlash/DataFlashUtility.h>
#include <QGCLoggingCategory.h>

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QStandardPaths>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

QGC_LOGGING_CATEGORY(LogAnalyzerLog, "qgc.agent.loganalyzer")

static constexpr int kFftSize = 4096;

LogAnalyzer::LogAnalyzer(QObject *parent)
    : QObject(parent)
{
}

bool LogAnalyzer::loadLog(const QString &filePath)
{
    _loaded = false;
    _errorString.clear();
    _accelSamples.clear();
    _gyroSamples.clear();
    _gpsSamples.clear();
    _batterySamples.clear();
    _pidSamples.clear();
    _ekfSamples.clear();
    _flightModeSamples.clear();
    _opticalFlowSamples.clear();
    _filePath = filePath;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        _errorString = QStringLiteral("无法打开文件: ") + filePath;
        emit analysisError(_errorString);
        return false;
    }

    _fileData = f.readAll();
    f.close();

    if (_fileData.isEmpty()) {
        _errorString = QStringLiteral("文件为空: ") + filePath;
        emit analysisError(_errorString);
        return false;
    }

    if (!ULogUtility::isValidHeader(_fileData.constData(), _fileData.size())) {
        // Try DataFlash (ArduPilot .bin) as fallback
        if (DataFlashUtility::isValidHeader(_fileData.constData(), _fileData.size())) {
            _extractDataFlashData();
            _loaded = true;
            qCDebug(LogAnalyzerLog) << "Loaded DataFlash" << filePath
                                    << "accel:" << _accelSamples.size()
                                    << "gyro:" << _gyroSamples.size()
                                    << "gps:" << _gpsSamples.size()
                                    << "battery:" << _batterySamples.size();
            return true;
        }
        _errorString = QStringLiteral("不是有效的飞行日志文件 (不支持此格式): ") + filePath;
        emit analysisError(_errorString);
        return false;
    }

    _extractImuData();
    _extractGpsData();
    _extractBatteryData();
    _extractEkfData();
    _extractFlightModeData();
    _extractOpticalFlowData();

    _loaded = true;
    qCDebug(LogAnalyzerLog) << "Loaded" << filePath
                            << "accel:" << _accelSamples.size()
                            << "gyro:" << _gyroSamples.size()
                            << "gps:" << _gpsSamples.size()
                            << "battery:" << _batterySamples.size()
                            << "ekf:" << _ekfSamples.size()
                            << "mode:" << _flightModeSamples.size()
                            << "flow:" << _opticalFlowSamples.size();
    return true;
}

// ============================================================================
// Data extraction
// ============================================================================

static bool _tryExtractSensor(const QByteArray &data, const char *topic,
                               const char *tsField,
                               std::function<void(uint64_t, float, float, float)> cb,
                               QString &err)
{
    return ULogUtility::iterateMessages(
        data.constData(), data.size(), topic,
        [&](const ulog_cpp::TypedDataView &s) {
            uint64_t ts = s.at(tsField).as<uint64_t>();
            float x = s.at("x").as<float>();
            float y = s.at("y").as<float>();
            float z = s.at("z").as<float>();
            cb(ts, x, y, z);
            return true;
        }, err);
}

bool LogAnalyzer::_extractImuData()
{
    QString err;

    _tryExtractSensor(_fileData, "sensor_accel", "timestamp",
        [this](uint64_t ts, float x, float y, float z) {
            _accelSamples.append({ts, x, y, z});
        }, err);

    if (_accelSamples.isEmpty()) {
        err.clear();
        _tryExtractSensor(_fileData, "sensor_accel", "timestamp_sample",
            [this](uint64_t ts, float x, float y, float z) {
                _accelSamples.append({ts, x, y, z});
            }, err);
    }

    _tryExtractSensor(_fileData, "sensor_gyro", "timestamp",
        [this](uint64_t ts, float x, float y, float z) {
            _gyroSamples.append({ts, x, y, z});
        }, err);

    if (_gyroSamples.isEmpty()) {
        err.clear();
        _tryExtractSensor(_fileData, "sensor_gyro", "timestamp_sample",
            [this](uint64_t ts, float x, float y, float z) {
                _gyroSamples.append({ts, x, y, z});
            }, err);
    }

    return !_accelSamples.isEmpty() || !_gyroSamples.isEmpty();
}

bool LogAnalyzer::_extractGpsData()
{
    QString err;
    ULogUtility::iterateMessages(
        _fileData.constData(), _fileData.size(), "vehicle_gps_position",
        [this](const ulog_cpp::TypedDataView &s) {
            GpsSample g;
            g.timestampUs = s.at("timestamp").as<uint64_t>();
            g.lat = s.at("lat").as<double>();
            g.lon = s.at("lon").as<double>();
            g.alt = s.at("alt").as<float>();
            g.hdop = s.at("hdop").as<float>();
            g.vdop = s.at("vdop").as<float>();
            g.satellitesUsed = static_cast<int>(s.at("satellites_used").as<uint8_t>());
            g.fixType = static_cast<int>(s.at("fix_type").as<uint8_t>());
            _gpsSamples.append(g);
            return true;
        }, err);
    return !_gpsSamples.isEmpty();
}

bool LogAnalyzer::_extractBatteryData()
{
    QString err;
    ULogUtility::iterateMessages(
        _fileData.constData(), _fileData.size(), "battery_status",
        [this](const ulog_cpp::TypedDataView &s) {
            BatterySample b;
            b.timestampUs = s.at("timestamp").as<uint64_t>();
            b.voltage = s.at("voltage_v").as<float>();
            b.current = s.at("current_a").as<float>();
            b.remaining = s.at("remaining").as<float>();
            if (s.hasField("temperature")) {
                b.temperature = s.at("temperature").as<float>();
            }
            _batterySamples.append(b);
            return true;
        }, err);
    return !_batterySamples.isEmpty();
}

bool LogAnalyzer::_extractEkfData()
{
    QString err;
    ULogUtility::iterateMessages(
        _fileData.constData(), _fileData.size(), "estimator_status",
        [this](const ulog_cpp::TypedDataView &s) {
            EkfSample e;
            e.timestampUs = s.at("timestamp").as<uint64_t>();
            if (s.hasField("vel_test_ratio")) e.velTestRatio = s.at("vel_test_ratio").as<float>();
            if (s.hasField("pos_test_ratio")) e.posTestRatio = s.at("pos_test_ratio").as<float>();
            if (s.hasField("hgt_test_ratio")) e.hgtTestRatio = s.at("hgt_test_ratio").as<float>();
            if (s.hasField("mag_test_ratio"))      e.magTestRatio = s.at("mag_test_ratio").as<float>();
            else if (s.hasField("mag_test"))       e.magTestRatio = s.at("mag_test").as<float>();
            if (s.hasField("hagl_test_ratio")) e.haglTestRatio = s.at("hagl_test_ratio").as<float>();
            if (s.hasField("tas_test_ratio")) e.tasTestRatio = s.at("tas_test_ratio").as<float>();
            if (s.hasField("gps_check_fail_flags")) {
                e.gpsCheckFailFlags = static_cast<int>(s.at("gps_check_fail_flags").as<uint16_t>());
            }
            _ekfSamples.append(e);
            return true;
        }, err);
    return !_ekfSamples.isEmpty();
}

// ============================================================================
// Flight mode extraction
// ============================================================================

QString LogAnalyzer::_px4ModeName(int navState)
{
    static const QHash<int, QString> map = {
        {0, QStringLiteral("Manual")},     {1, QStringLiteral("AltHold")},
        {2, QStringLiteral("PosHold")},    {3, QStringLiteral("Mission")},
        {4, QStringLiteral("Loiter")},     {5, QStringLiteral("RTL")},
        {6, QStringLiteral("Acro")},       {7, QStringLiteral("Offboard")},
        {8, QStringLiteral("Stabilized")}, {9, QStringLiteral("Rattitude")},
        {10, QStringLiteral("Takeoff")},   {11, QStringLiteral("Land")},
        {12, QStringLiteral("Descend")},   {14, QStringLiteral("Follow")},
        {15, QStringLiteral("Precland")},  {16, QStringLiteral("Orbit")},
    };
    return map.value(navState, QStringLiteral("Unknown(%1)").arg(navState));
}

QString LogAnalyzer::_arduModeName(int customMode)
{
    static const QHash<int, QString> map = {
        {0, QStringLiteral("Stabilize")},  {1, QStringLiteral("Acro")},
        {2, QStringLiteral("AltHold")},    {3, QStringLiteral("Auto")},
        {4, QStringLiteral("Guided")},     {5, QStringLiteral("Loiter")},
        {6, QStringLiteral("RTL")},        {7, QStringLiteral("Circle")},
        {9, QStringLiteral("Land")},       {13, QStringLiteral("Sport")},
        {15, QStringLiteral("AutoTune")},  {16, QStringLiteral("PosHold")},
        {17, QStringLiteral("Brake")},     {21, QStringLiteral("Smart_RTL")},
        {23, QStringLiteral("Follow")},
    };
    return map.value(customMode, QStringLiteral("Unknown(%1)").arg(customMode));
}

bool LogAnalyzer::_extractFlightModeData()
{
    QString err;

    // PX4: vehicle_status.nav_state
    ULogUtility::iterateMessages(
        _fileData.constData(), _fileData.size(), "vehicle_status",
        [this](const ulog_cpp::TypedDataView &s) {
            FlightModeSample m;
            m.timestampUs = s.at("timestamp").as<uint64_t>();
            m.navState = static_cast<int>(s.at("nav_state").as<uint8_t>());
            m.modeName = _px4ModeName(m.navState);
            _flightModeSamples.append(m);
            return true;
        }, err);

    // If no PX4 data found, try ArduPilot MODE message (handled in DataFlash path)
    return !_flightModeSamples.isEmpty();
}

bool LogAnalyzer::_extractOpticalFlowData()
{
    QString err;

    // Try both possible ULog topic names
    for (const char *topic : {"vehicle_optical_flow", "optical_flow"}) {
        ULogUtility::iterateMessages(
            _fileData.constData(), _fileData.size(), topic,
            [this](const ulog_cpp::TypedDataView &s) {
                OpticalFlowSample o;
                o.timestampUs = s.at("timestamp").as<uint64_t>();

                // PX4 vehicle_optical_flow uses pixel_flow[x/y]
                if (s.hasField("pixel_flow[0]")) {
                    o.flowX = s.at("pixel_flow[0]").as<float>();
                    o.flowY = s.at("pixel_flow[1]").as<float>();
                } else if (s.hasField("flow_x")) {
                    o.flowX = s.at("flow_x").as<float>();
                    o.flowY = s.at("flow_y").as<float>();
                }

                if (s.hasField("quality")) o.quality = s.at("quality").as<float>();
                if (s.hasField("ground_distance_m")) o.groundDistanceM = s.at("ground_distance_m").as<float>();

                _opticalFlowSamples.append(o);
                return true;
            }, err);

        if (!_opticalFlowSamples.isEmpty()) break;
        err.clear();
    }

    return !_opticalFlowSamples.isEmpty();
}

QVector<LogAnalyzer::SensorSample> LogAnalyzer::_filterSamplesByMode(
    const QVector<SensorSample> &samples, const QString &modeName) const
{
    if (_flightModeSamples.isEmpty()) return samples;

    // Build time ranges for the requested mode
    struct Range { uint64_t start, end; };
    QVector<Range> ranges;

    QString currentMode;
    uint64_t modeStartUs = 0;
    for (int i = 0; i < _flightModeSamples.size(); i++) {
        const auto &m = _flightModeSamples[i];
        if (m.modeName != currentMode) {
            if (currentMode == modeName && i > 0) {
                ranges.append({modeStartUs, m.timestampUs});
            }
            currentMode = m.modeName;
            modeStartUs = m.timestampUs;
        }
    }
    // Close last range
    if (currentMode == modeName && !_flightModeSamples.isEmpty()) {
        ranges.append({modeStartUs, UINT64_MAX});
    }

    if (ranges.isEmpty()) return {};

    // Filter samples that fall within any range
    QVector<SensorSample> filtered;
    for (const auto &s : samples) {
        for (const auto &r : ranges) {
            if (s.timestampUs >= r.start && s.timestampUs < r.end) {
                filtered.append(s);
                break;
            }
        }
    }
    return filtered;
}

// ============================================================================
// Optical flow analysis
// ============================================================================

LogAnalyzer::OpticalFlowResult LogAnalyzer::_computeOpticalFlowStats()
{
    OpticalFlowResult r;
    r.totalSamples = _opticalFlowSamples.size();
    if (_opticalFlowSamples.isEmpty()) return r;
    r.hasData = true;

    if (_opticalFlowSamples.size() >= 2) {
        r.durationSec = static_cast<float>(
            _opticalFlowSamples.last().timestampUs - _opticalFlowSamples.first().timestampUs) / 1e6f;
    }

    float qSum = 0, flowMagSum = 0, distSum = 0;
    r.groundDistMin = 1e6f;
    int lowQ = 0;

    for (const auto &s : _opticalFlowSamples) {
        if (s.quality < r.qualityMin) r.qualityMin = s.quality;
        if (s.quality > r.qualityMax) r.qualityMax = s.quality;
        qSum += s.quality;
        if (s.quality < 100.0f) lowQ++;

        float mag = std::sqrt(s.flowX * s.flowX + s.flowY * s.flowY);
        flowMagSum += mag;
        if (mag > r.flowMagMax) r.flowMagMax = mag;

        if (s.groundDistanceM > 0) {
            if (s.groundDistanceM < r.groundDistMin) r.groundDistMin = s.groundDistanceM;
            if (s.groundDistanceM > r.groundDistMax) r.groundDistMax = s.groundDistanceM;
            distSum += s.groundDistanceM;
        }
    }

    const float n = static_cast<float>(r.totalSamples);
    r.qualityMean = qSum / n;
    r.lowQualityCount = lowQ;
    r.flowMagMean = flowMagSum / n;
    if (distSum > 0) r.groundDistMean = distSum / n;

    return r;
}

// ============================================================================
// Analysis scope helpers
// ============================================================================

void LogAnalyzer::setAnalysisTimeRange(double startSec, double endSec)
{
    _scopeStartSec = startSec;
    _scopeEndSec = endSec;
}

void LogAnalyzer::setAnalysisDimensions(const QStringList &dims)
{
    _scopeDimensions = dims;
}

// ============================================================================
// DataFlash extraction (ArduPilot .bin logs)
// ============================================================================

void LogAnalyzer::_extractDataFlashData()
{
    // Parse FMT messages first (two-pass: learn format definitions, then extract data)
    QMap<uint8_t, DataFlashUtility::MessageFormat> formats;
    DataFlashUtility::parseFmtMessages(_fileData.constData(), _fileData.size(), formats);

    // Map format name → type for quick lookup
    QHash<QString, uint8_t> nameToType;
    for (auto it = formats.constBegin(); it != formats.constEnd(); ++it) {
        nameToType[it.value().name.toUpper()] = it.key();
    }

    // Known ArduPilot message names for each data type
    const QStringList imuNames = {"IMU", "IMU2", "ACC1", "GYR1"};
    const QStringList gpsNames = {"GPS", "GPS2"};
    const QStringList batNames = {"BAT", "BATT"};
    const QStringList ekfNames = {"NKF1", "XKF1"};
    const QStringList modeNames = {"MODE"};
    const QStringList flowNames = {"OFD", "FLOW"};

    DataFlashUtility::iterateMessages(
        _fileData.constData(), _fileData.size(), formats,
        [&](uint8_t /*msgType*/, const char *payload, int /*payloadSize*/, const DataFlashUtility::MessageFormat &fmt) {
            auto fields = DataFlashUtility::parseMessage(payload, fmt);
            QString name = fmt.name.toUpper();

            if (imuNames.contains(name)) {
                SensorSample s;
                // Try both ULog-style and DataFlash-style timestamp field names
                if (fields.contains("TimeUS")) s.timestampUs = static_cast<uint64_t>(fields["TimeUS"].toLongLong());
                else if (fields.contains("SampleTimeUS")) s.timestampUs = static_cast<uint64_t>(fields["SampleTimeUS"].toLongLong());
                else s.timestampUs = _accelSamples.size(); // fallback

                // Accel: try AccX/Y/Z first, then GyrX/Y/Z (some messages carry both)
                if (fields.contains("AccX") && fields.contains("AccY") && fields.contains("AccZ")) {
                    s.x = fields["AccX"].toFloat();
                    s.y = fields["AccY"].toFloat();
                    s.z = fields["AccZ"].toFloat();
                    _accelSamples.append(s);
                }
                // Gyro: if message has gyro fields, extract separately
                if (fields.contains("GyrX") && fields.contains("GyrY") && fields.contains("GyrZ")) {
                    SensorSample g;
                    g.timestampUs = s.timestampUs;
                    g.x = fields["GyrX"].toFloat();
                    g.y = fields["GyrY"].toFloat();
                    g.z = fields["GyrZ"].toFloat();
                    _gyroSamples.append(g);
                }
            }
            else if (gpsNames.contains(name)) {
                GpsSample g;
                if (fields.contains("TimeUS")) g.timestampUs = static_cast<uint64_t>(fields["TimeUS"].toLongLong());
                else g.timestampUs = _gpsSamples.size();
                // ArduPilot DataFlash GPS fields
                if (fields.contains("Lat")) g.lat = fields["Lat"].toDouble();
                else if (fields.contains("Lat")) g.lat = fields["Lat"].toDouble();
                if (fields.contains("Lng")) g.lon = fields["Lng"].toDouble();
                else if (fields.contains("Lon")) g.lon = fields["Lon"].toDouble();
                if (fields.contains("Alt")) g.alt = fields["Alt"].toFloat();
                if (fields.contains("HDop")) g.hdop = fields["HDop"].toFloat();
                if (fields.contains("NSats")) g.satellitesUsed = fields["NSats"].toInt();
                else if (fields.contains("NumSats")) g.satellitesUsed = fields["NumSats"].toInt();
                if (fields.contains("Fix")) g.fixType = fields["Fix"].toInt();
                // GPS lat/lon might be in degrees (ULog) or degrees*1e7 (DF)
                if (std::abs(g.lat) > 90.0) { g.lat /= 1e7; g.lon /= 1e7; }
                _gpsSamples.append(g);
            }
            else if (batNames.contains(name)) {
                BatterySample b;
                if (fields.contains("TimeUS")) b.timestampUs = static_cast<uint64_t>(fields["TimeUS"].toLongLong());
                else b.timestampUs = _batterySamples.size();
                if (fields.contains("Volt")) b.voltage = fields["Volt"].toFloat();
                else if (fields.contains("Voltage")) b.voltage = fields["Voltage"].toFloat();
                // ArduPilot stores voltage in centi-volts, normalize
                if (b.voltage > 100.0f) b.voltage /= 100.0f;
                if (fields.contains("Curr")) b.current = fields["Curr"].toFloat();
                else if (fields.contains("Current")) b.current = fields["Current"].toFloat();
                if (b.current > 100.0f) b.current /= 100.0f; // centi-amps
                if (fields.contains("Remaining")) b.remaining = fields["Remaining"].toFloat();
                if (fields.contains("Temp")) b.temperature = fields["Temp"].toFloat();
                _batterySamples.append(b);
            }
            else if (ekfNames.contains(name)) {
                EkfSample e;
                if (fields.contains("TimeUS")) e.timestampUs = static_cast<uint64_t>(fields["TimeUS"].toLongLong());
                else e.timestampUs = _ekfSamples.size();
                if (fields.contains("IVN")) e.velTestRatio = fields["IVN"].toFloat();
                if (fields.contains("IPN")) e.posTestRatio = fields["IPN"].toFloat();
                if (fields.contains("IAN")) e.hgtTestRatio = fields["IAN"].toFloat();
                if (fields.contains("IMX")) e.magTestRatio = fields["IMX"].toFloat();
                if (fields.contains("HAGL")) e.haglTestRatio = fields["HAGL"].toFloat();
                if (fields.contains("TAS")) e.tasTestRatio = fields["TAS"].toFloat();
                if (fields.contains("gpsCheckFailFlags"))
                    e.gpsCheckFailFlags = fields["gpsCheckFailFlags"].toInt();
                _ekfSamples.append(e);
            }
            else if (modeNames.contains(name)) {
                FlightModeSample m;
                if (fields.contains("TimeUS")) m.timestampUs = static_cast<uint64_t>(fields["TimeUS"].toLongLong());
                else m.timestampUs = _flightModeSamples.size();
                // ArduPilot MODE message: ModeNum or Mode string
                if (fields.contains("ModeNum")) {
                    m.navState = fields["ModeNum"].toInt();
                    m.modeName = _arduModeName(m.navState);
                } else if (fields.contains("Mode")) {
                    m.modeName = fields["Mode"].toString().toUpper();
                }
                _flightModeSamples.append(m);
            }
            else if (flowNames.contains(name)) {
                OpticalFlowSample o;
                if (fields.contains("TimeUS")) o.timestampUs = static_cast<uint64_t>(fields["TimeUS"].toLongLong());
                else o.timestampUs = _opticalFlowSamples.size();
                if (fields.contains("FlowX")) o.flowX = fields["FlowX"].toFloat();
                if (fields.contains("FlowY")) o.flowY = fields["FlowY"].toFloat();
                if (fields.contains("Qual")) o.quality = fields["Qual"].toFloat();
                else if (fields.contains("Quality")) o.quality = fields["Quality"].toFloat();
                if (fields.contains("GD")) o.groundDistanceM = fields["GD"].toFloat();
                else if (fields.contains("GndDist")) o.groundDistanceM = fields["GndDist"].toFloat();
                _opticalFlowSamples.append(o);
            }
            return true;
        });
}

// ============================================================================
// Time-series serialization for QML visualization
// ============================================================================

LogAnalyzer::TimeSeriesData LogAnalyzer::timeSeriesJson(int maxPoints) const
{
    TimeSeriesData d;

    // Helper: sanitize float for JSON/QML — replace NaN/Inf with 0
    auto safeVal = [](double v) -> double {
        if (!std::isfinite(v)) return 0.0;
        return v;
    };

    // GPS sentinel: hdop/vdop default to 99 when no data
    auto safeHdop = [](float v) -> double {
        if (!std::isfinite(v) || v >= 99.0f) return 0.0;
        return static_cast<double>(v);
    };

    // Determine the global base timestamp (earliest sample across all types)
    uint64_t baseUs = UINT64_MAX;
    if (!_accelSamples.isEmpty()) baseUs = qMin(baseUs, _accelSamples.first().timestampUs);
    if (!_gyroSamples.isEmpty()) baseUs = qMin(baseUs, _gyroSamples.first().timestampUs);
    if (!_gpsSamples.isEmpty()) baseUs = qMin(baseUs, _gpsSamples.first().timestampUs);
    if (!_batterySamples.isEmpty()) baseUs = qMin(baseUs, _batterySamples.first().timestampUs);
    if (!_ekfSamples.isEmpty()) baseUs = qMin(baseUs, _ekfSamples.first().timestampUs);
    if (!_pidSamples.isEmpty()) baseUs = qMin(baseUs, _pidSamples.first().timestampUs);
    if (!_opticalFlowSamples.isEmpty()) baseUs = qMin(baseUs, _opticalFlowSamples.first().timestampUs);
    if (!_flightModeSamples.isEmpty()) baseUs = qMin(baseUs, _flightModeSamples.first().timestampUs);
    if (baseUs == UINT64_MAX) baseUs = 0;

    // Helper: stride-based subsample index list
    auto subsampleIndices = [maxPoints](int total) -> QVector<int> {
        if (total <= maxPoints || maxPoints <= 0) {
            QVector<int> idx(total);
            std::iota(idx.begin(), idx.end(), 0);
            return idx;
        }
        QVector<int> idx;
        idx.reserve(maxPoints);
        int stride = total / maxPoints;
        idx.append(0);
        for (int i = stride; i < total - 1; i += stride) {
            idx.append(i);
        }
        idx.append(total - 1);
        return idx;
    };

    // Accel
    {
        auto idx = subsampleIndices(_accelSamples.size());
        for (int i : idx) {
            const auto &s = _accelSamples[i];
            d.accelSeries.append(QJsonObject{
                {QStringLiteral("t"), safeVal(static_cast<double>(s.timestampUs - baseUs) / 1e6)},
                {QStringLiteral("x"), safeVal(static_cast<double>(s.x))},
                {QStringLiteral("y"), safeVal(static_cast<double>(s.y))},
                {QStringLiteral("z"), safeVal(static_cast<double>(s.z))}
            });
        }
    }

    // Gyro
    {
        auto idx = subsampleIndices(_gyroSamples.size());
        for (int i : idx) {
            const auto &s = _gyroSamples[i];
            d.gyroSeries.append(QJsonObject{
                {QStringLiteral("t"), safeVal(static_cast<double>(s.timestampUs - baseUs) / 1e6)},
                {QStringLiteral("x"), safeVal(static_cast<double>(s.x))},
                {QStringLiteral("y"), safeVal(static_cast<double>(s.y))},
                {QStringLiteral("z"), safeVal(static_cast<double>(s.z))}
            });
        }
    }

    // GPS
    {
        auto idx = subsampleIndices(_gpsSamples.size());
        for (int i : idx) {
            const auto &s = _gpsSamples[i];
            d.gpsSeries.append(QJsonObject{
                {QStringLiteral("t"), safeVal(static_cast<double>(s.timestampUs - baseUs) / 1e6)},
                {QStringLiteral("lat"), safeVal(s.lat)},
                {QStringLiteral("lon"), safeVal(s.lon)},
                {QStringLiteral("alt"), safeVal(static_cast<double>(s.alt))},
                {QStringLiteral("hdop"), safeHdop(s.hdop)},
                {QStringLiteral("sat"), s.satellitesUsed},
                {QStringLiteral("fix"), s.fixType}
            });
        }
    }

    // Battery
    {
        auto idx = subsampleIndices(_batterySamples.size());
        for (int i : idx) {
            const auto &s = _batterySamples[i];
            d.batterySeries.append(QJsonObject{
                {QStringLiteral("t"), safeVal(static_cast<double>(s.timestampUs - baseUs) / 1e6)},
                {QStringLiteral("v"), safeVal(static_cast<double>(s.voltage))},
                {QStringLiteral("a"), safeVal(static_cast<double>(s.current))},
                {QStringLiteral("pct"), safeVal(static_cast<double>(s.remaining))},
                {QStringLiteral("temp"), safeVal(static_cast<double>(s.temperature))}
            });
        }
    }

    // EKF
    {
        auto idx = subsampleIndices(_ekfSamples.size());
        for (int i : idx) {
            const auto &s = _ekfSamples[i];
            d.ekfSeries.append(QJsonObject{
                {QStringLiteral("t"), safeVal(static_cast<double>(s.timestampUs - baseUs) / 1e6)},
                {QStringLiteral("vel"), safeVal(static_cast<double>(s.velTestRatio))},
                {QStringLiteral("pos"), safeVal(static_cast<double>(s.posTestRatio))},
                {QStringLiteral("hgt"), safeVal(static_cast<double>(s.hgtTestRatio))},
                {QStringLiteral("mag"), safeVal(static_cast<double>(s.magTestRatio))}
            });
        }
    }

    // PID
    {
        auto idx = subsampleIndices(_pidSamples.size());
        for (int i : idx) {
            const auto &s = _pidSamples[i];
            d.pidSeries.append(QJsonObject{
                {QStringLiteral("t"), safeVal(static_cast<double>(s.timestampUs - baseUs) / 1e6)},
                {QStringLiteral("rollDes"), safeVal(static_cast<double>(s.rollSetpoint))},
                {QStringLiteral("rollAct"), safeVal(static_cast<double>(s.rollActual))},
                {QStringLiteral("pitchDes"), safeVal(static_cast<double>(s.pitchSetpoint))},
                {QStringLiteral("pitchAct"), safeVal(static_cast<double>(s.pitchActual))},
                {QStringLiteral("yawDes"), safeVal(static_cast<double>(s.yawSetpoint))},
                {QStringLiteral("yawAct"), safeVal(static_cast<double>(s.yawActual))}
            });
        }
    }

    // Optical flow
    {
        auto idx = subsampleIndices(_opticalFlowSamples.size());
        for (int i : idx) {
            const auto &s = _opticalFlowSamples[i];
            d.opticalFlowSeries.append(QJsonObject{
                {QStringLiteral("t"), safeVal(static_cast<double>(s.timestampUs - baseUs) / 1e6)},
                {QStringLiteral("fx"), safeVal(static_cast<double>(s.flowX))},
                {QStringLiteral("fy"), safeVal(static_cast<double>(s.flowY))},
                {QStringLiteral("q"), safeVal(static_cast<double>(s.quality))},
                {QStringLiteral("dist"), safeVal(static_cast<double>(s.groundDistanceM))}
            });
        }
    }

    // Flight modes (typically very few, no subsampling needed)
    for (const auto &s : _flightModeSamples) {
        d.flightModeSeries.append(QJsonObject{
            {QStringLiteral("t"), static_cast<double>(s.timestampUs - baseUs) / 1e6},
            {QStringLiteral("navState"), s.navState},
            {QStringLiteral("name"), s.modeName}
        });
    }

    return d;
}

// ============================================================================
// Report persistence
// ============================================================================

static QString logSaveDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.agentqgc/logs");
    QDir().mkpath(dir);
    return dir;
}

void LogAnalyzer::_saveReport(const QString &report)
{
    QFileInfo fi(_filePath);
    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString outPath = logSaveDir() + "/analysis_" + fi.baseName() + "_" + ts + ".txt";
    QFile f(outPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(report.toUtf8());
        f.close();
        _lastSavedReportPath = outPath;
    }
}

QString LogAnalyzer::lastSavedReportPath() const { return _lastSavedReportPath; }

void LogAnalyzer::setVibrationThresholds(float accelGood, float accelWarn,
                                          float gyroGood, float gyroWarn)
{
    _accelGoodThreshold = qMax(0.0f, accelGood);
    _accelWarnThreshold = qMax(_accelGoodThreshold, accelWarn);
    _gyroGoodThreshold = qMax(0.0f, gyroGood);
    _gyroWarnThreshold = qMax(_gyroGoodThreshold, gyroWarn);
}

// ============================================================================
// FFT (Cooley-Tukey radix-2, in-place)
// ============================================================================

void LogAnalyzer::_fft(std::vector<std::complex<float>> &data)
{
    int n = static_cast<int>(data.size());
    if (n <= 1) return;

    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }

    for (int len = 2; len <= n; len <<= 1) {
        float angle = 2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
        std::complex<float> wlen(std::cos(angle), -std::sin(angle));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (int j = 0; j < len / 2; j++) {
                std::complex<float> u = data[i + j];
                std::complex<float> v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// ============================================================================
// Vibration analysis
// ============================================================================

int LogAnalyzer::_findMaxRmsSegment(const QVector<SensorSample> &samples, int segmentSize)
{
    const int n = samples.size();
    if (n <= segmentSize) return 0;

    constexpr int kMaxSegments = 10;
    const int stride = segmentSize / 2; // 50% overlap
    const int numSegments = std::min(n / stride - 1, kMaxSegments);

    int bestStart = 0;
    float bestRms = 0;

    for (int i = 0; i < numSegments; i++) {
        const int start = i * stride;
        float sumSq = 0;
        for (int j = 0; j < segmentSize && (start + j) < n; j++) {
            const auto &s = samples[start + j];
            sumSq += s.x * s.x + s.y * s.y + s.z * s.z;
        }
        const float rms = std::sqrt(sumSq / static_cast<float>(std::min(segmentSize, n - start)));
        if (rms > bestRms) {
            bestRms = rms;
            bestStart = start;
        }
    }
    return bestStart;
}

LogAnalyzer::VibrationResult LogAnalyzer::_computeVibration(const QVector<SensorSample> &samples)
{
    VibrationResult r;
    r.sampleCount = samples.size();
    if (samples.isEmpty()) return r;

    if (samples.size() >= 2) {
        r.durationSec = static_cast<float>(samples.last().timestampUs - samples.first().timestampUs) / 1e6f;
    }
    r.sampleRateHz = r.durationSec > 0 ? static_cast<float>(r.sampleCount) / r.durationSec : 0;

    float sumX = 0, sumY = 0, sumZ = 0;
    float sqX = 0, sqY = 0, sqZ = 0;
    int clips = 0;

    for (const auto &s : samples) {
        float ax = std::abs(s.x), ay = std::abs(s.y), az = std::abs(s.z);
        sumX += s.x; sumY += s.y; sumZ += s.z;
        sqX += s.x * s.x; sqY += s.y * s.y; sqZ += s.z * s.z;
        if (ax > r.maxX) r.maxX = ax;
        if (ay > r.maxY) r.maxY = ay;
        if (az > r.maxZ) r.maxZ = az;
        if (ax > 156.0f || ay > 156.0f || az > 156.0f) clips++;
    }

    float n = static_cast<float>(r.sampleCount);
    r.meanX = sumX / n; r.meanY = sumY / n; r.meanZ = sumZ / n;
    r.rmsX = std::sqrt(sqX / n); r.rmsY = std::sqrt(sqY / n); r.rmsZ = std::sqrt(sqZ / n);
    r.clipCount = clips;

    if (r.sampleCount >= kFftSize && r.sampleRateHz > 0) {
        const int start = _findMaxRmsSegment(samples, kFftSize);
        const float fs = r.sampleRateHz;

        auto doFft = [&](const std::vector<float> &buf, float &peakFreq, float &peakMag,
                          float &eLow, float &eMid, float &eHigh,
                          QVector<SpectrumPoint> *spectrumOut) {
            const float mean = std::accumulate(buf.begin(), buf.end(), 0.0f) / static_cast<float>(buf.size());
            std::vector<float> dcRemoved(kFftSize);
            for (int i = 0; i < kFftSize; i++) {
                const float w = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (kFftSize - 1)));
                dcRemoved[i] = (buf[i] - mean) * w;
            }
            std::vector<std::complex<float>> fftData(kFftSize);
            for (int i = 0; i < kFftSize; i++) fftData[i] = std::complex<float>(dcRemoved[i], 0);
            _fft(fftData);

            const int halfN = kFftSize / 2;
            const float binHz = fs / static_cast<float>(kFftSize);
            peakFreq = 0; peakMag = 0; eLow = 0; eMid = 0; eHigh = 0;

            // Only output spectrum up to ~500 Hz (~250 bins at typical ~800 Hz sample rate)
            const int maxSpectrumBins = std::min(halfN, static_cast<int>(500.0f / binHz) + 1);

            for (int i = 1; i <= halfN; i++) {
                const float mag = std::abs(fftData[i]) / static_cast<float>(kFftSize);
                const float freq = static_cast<float>(i) * binHz;
                if (mag > peakMag) { peakMag = mag; peakFreq = freq; }
                const float energy = mag * mag;
                if (freq <= 50.0f) eLow += energy;
                else if (freq <= 200.0f) eMid += energy;
                else if (freq <= 500.0f) eHigh += energy;

                if (spectrumOut && i <= maxSpectrumBins) {
                    spectrumOut->append({freq, mag});
                }
            }
        };

        std::vector<float> bx(kFftSize), by(kFftSize), bz(kFftSize);
        for (int i = 0; i < kFftSize; i++) {
            const auto &s = samples[start + i];
            bx[i] = s.x; by[i] = s.y; bz[i] = s.z;
        }

        QVector<SpectrumPoint> *sx = _includeSpectrum ? &r.spectrumX : nullptr;
        QVector<SpectrumPoint> *sy = _includeSpectrum ? &r.spectrumY : nullptr;
        QVector<SpectrumPoint> *sz = _includeSpectrum ? &r.spectrumZ : nullptr;

        doFft(bx, r.peakFreqX, r.peakMagX, r.energyXLow, r.energyXMid, r.energyXHigh, sx);
        doFft(by, r.peakFreqY, r.peakMagY, r.energyYLow, r.energyYMid, r.energyYHigh, sy);
        doFft(bz, r.peakFreqZ, r.peakMagZ, r.energyZLow, r.energyZMid, r.energyZHigh, sz);
    }

    return r;
}

// ============================================================================
// GPS health analysis
// ============================================================================

LogAnalyzer::GpsResult LogAnalyzer::_computeGpsStats()
{
    GpsResult r;
    r.totalSamples = _gpsSamples.size();
    if (_gpsSamples.isEmpty()) return r;
    r.hasData = true;

    if (_gpsSamples.size() >= 2) {
        r.durationSec = static_cast<float>(_gpsSamples.last().timestampUs - _gpsSamples.first().timestampUs) / 1e6f;
    }

    float hdopSum = 0, vdopSum = 0;
    int satSum = 0;
    double lastLat = 0, lastLon = 0;
    bool hasLast = false;

    for (const auto &s : _gpsSamples) {
        if (s.hdop < r.hdopMin) r.hdopMin = s.hdop;
        if (s.hdop > r.hdopMax) r.hdopMax = s.hdop;
        hdopSum += s.hdop;

        if (s.vdop < r.vdopMin) r.vdopMin = s.vdop;
        if (s.vdop > r.vdopMax) r.vdopMax = s.vdop;
        vdopSum += s.vdop;

        if (s.satellitesUsed < r.satMin) r.satMin = s.satellitesUsed;
        if (s.satellitesUsed > r.satMax) r.satMax = s.satellitesUsed;
        satSum += s.satellitesUsed;

        if (s.fixType >= 0 && s.fixType <= 7) r.fixTypeDist[s.fixType]++;

        // Position jump detection (>5m in one sample at 5Hz ~= 25m/s jump)
        if (hasLast) {
            const double dLat = (s.lat - lastLat) * 111320.0;
            const double dLon = (s.lon - lastLon) * 111320.0 * std::cos(s.lat * M_PI / 180.0);
            const double jump = std::sqrt(dLat * dLat + dLon * dLon);
            if (jump > 5.0) r.positionJumps++;
        }
        lastLat = s.lat; lastLon = s.lon; hasLast = true;
    }

    float n = static_cast<float>(r.totalSamples);
    r.hdopMean = hdopSum / n;
    r.vdopMean = vdopSum / n;
    r.satMean = static_cast<float>(satSum) / n;

    // HDOP spike = worst HDOP above mean
    r.maxHdopSpike = r.hdopMax - r.hdopMean;

    // Fix type percentages
    int totalFix = 0;
    for (int i = 0; i < 8; i++) totalFix += r.fixTypeDist[i];
    if (totalFix > 0) {
        r.noFixPct = 100.0f * static_cast<float>(r.fixTypeDist[0]) / static_cast<float>(totalFix);
        r.fix2dPct = 100.0f * static_cast<float>(r.fixTypeDist[2]) / static_cast<float>(totalFix);
        r.fix3dPct = 100.0f * static_cast<float>(r.fixTypeDist[3]) / static_cast<float>(totalFix);
        r.fixRtkPct = 100.0f * static_cast<float>(r.fixTypeDist[4] + r.fixTypeDist[5]) / static_cast<float>(totalFix);
    }

    return r;
}

// ============================================================================
// Battery performance analysis
// ============================================================================

LogAnalyzer::BatteryResult LogAnalyzer::_computeBatteryStats()
{
    BatteryResult r;
    r.totalSamples = _batterySamples.size();
    if (_batterySamples.isEmpty()) return r;
    r.hasData = true;

    if (_batterySamples.size() >= 2) {
        r.durationSec = static_cast<float>(_batterySamples.last().timestampUs - _batterySamples.first().timestampUs) / 1e6f;
    }

    r.voltageMin = 99; r.tempMin = 99;
    float voltSum = 0, currSum = 0;
    float capacityAs = 0; // integrated current in amp-seconds
    float loadedVoltMin = 99; // voltage under load (>2A)

    for (int i = 0; i < _batterySamples.size(); i++) {
        const auto &s = _batterySamples[i];

        if (s.voltage < r.voltageMin) r.voltageMin = s.voltage;
        if (s.voltage > r.voltageMax) r.voltageMax = s.voltage;
        voltSum += s.voltage;

        if (s.current < r.currentMin) r.currentMin = s.current;
        if (s.current > r.currentMax) r.currentMax = s.current;
        currSum += s.current;

        if (s.temperature < r.tempMin) r.tempMin = s.temperature;
        if (s.temperature > r.tempMax) r.tempMax = s.temperature;

        // Integrate current for capacity
        if (i > 0) {
            float dt = static_cast<float>(_batterySamples[i].timestampUs - _batterySamples[i - 1].timestampUs) / 1e6f;
            capacityAs += s.current * dt;
        }

        // Voltage under load
        if (s.current > 2.0f && s.voltage < loadedVoltMin) loadedVoltMin = s.voltage;
    }

    float n = static_cast<float>(r.totalSamples);
    r.voltageMean = voltSum / n;
    r.currentMean = currSum / n;
    r.voltageSag = r.voltageMax - (loadedVoltMin < 99 ? loadedVoltMin : r.voltageMin);
    r.capacityConsumedMah = capacityAs * 1000.0f / 3600.0f; // As → mAh

    // Start/end capacity
    r.capacityPctStart = _batterySamples.first().remaining * 100.0f;
    r.capacityPctEnd = _batterySamples.last().remaining * 100.0f;

    // Average C-rate (capacity consumed per hour / capacity)
    float capUsedPct = r.capacityPctStart - r.capacityPctEnd;
    if (r.durationSec > 0 && capUsedPct > 0) {
        r.avgDischargeRateC = (capUsedPct / 100.0f) / (r.durationSec / 3600.0f);
    }

    // Energy (Wh) = avg voltage * capacity (Ah)
    r.energyWh = r.voltageMean * r.capacityConsumedMah / 1000.0f;

    return r;
}

// ============================================================================
// PID / Rate controller analysis
// ============================================================================

LogAnalyzer::PidResult LogAnalyzer::_computePidStats()
{
    PidResult r;
    r.totalSamples = _pidSamples.size();
    if (_pidSamples.isEmpty()) return r;
    r.hasData = true;

    if (_pidSamples.size() >= 2) {
        r.durationSec = static_cast<float>(_pidSamples.last().timestampUs - _pidSamples.first().timestampUs) / 1e6f;
    }

    auto analyzeAxis = [](const QVector<PidSample> &samples,
                          std::function<float(const PidSample&)> setpointFn,
                          std::function<float(const PidSample&)> actualFn) -> PidAxisResult {
        PidAxisResult ax;
        float errSum = 0, errMax = 0;
        int n = samples.size();

        // Tracking error
        for (const auto &s : samples) {
            float err = std::abs(setpointFn(s) - actualFn(s));
            errSum += err * err;
            if (err > errMax) errMax = err;
        }
        ax.trackingErrorRms = std::sqrt(errSum / static_cast<float>(n));
        ax.maxError = errMax;

        // Steady-state error (last 20% of samples)
        int ssStart = n * 4 / 5;
        float ssSum = 0;
        for (int i = ssStart; i < n; i++) {
            ssSum += setpointFn(samples[i]) - actualFn(samples[i]);
        }
        ax.steadyStateError = ssSum / static_cast<float>(n - ssStart);

        // Overshoot detection
        int overshootCount = 0;
        for (int i = 1; i < n; i++) {
            float sp = setpointFn(samples[i]);
            float act = actualFn(samples[i]);
            if (std::abs(sp) < 0.01f) continue;
            if (std::abs(act) > std::abs(sp) * 1.3f) overshootCount++;
        }
        ax.overshootPct = 100.0f * static_cast<float>(overshootCount) / static_cast<float>(n);

        // Oscillation count (zero crossings of error derivative)
        int oscillations = 0;
        float prevErr = setpointFn(samples[0]) - actualFn(samples[0]);
        bool wasPositive = prevErr > 0;
        for (int i = 1; i < n; i++) {
            float err = setpointFn(samples[i]) - actualFn(samples[i]);
            bool isPositive = err > 0;
            if (isPositive != wasPositive && std::abs(err - prevErr) > 0.01f) {
                oscillations++;
                wasPositive = isPositive;
            }
        }
        ax.oscillationCount = oscillations;

        return ax;
    };

    r.roll  = analyzeAxis(_pidSamples, [](const PidSample &s) { return s.rollSetpoint; },  [](const PidSample &s) { return s.rollActual; });
    r.pitch = analyzeAxis(_pidSamples, [](const PidSample &s) { return s.pitchSetpoint; }, [](const PidSample &s) { return s.pitchActual; });
    r.yaw   = analyzeAxis(_pidSamples, [](const PidSample &s) { return s.yawSetpoint; },   [](const PidSample &s) { return s.yawActual; });

    return r;
}

// ============================================================================
// EKF health analysis
// ============================================================================

LogAnalyzer::EkfResult LogAnalyzer::_computeEkfStats()
{
    EkfResult r;
    r.totalSamples = _ekfSamples.size();
    if (_ekfSamples.isEmpty()) return r;
    r.hasData = true;

    if (_ekfSamples.size() >= 2) {
        r.durationSec = static_cast<float>(_ekfSamples.last().timestampUs - _ekfSamples.first().timestampUs) / 1e6f;
    }

    float velSum = 0, posSum = 0, hgtSum = 0, magSum = 0;
    const float kFaultThreshold = 0.5f; // test ratio above 0.5 = potential fault

    for (const auto &s : _ekfSamples) {
        float vel = std::abs(s.velTestRatio);
        float pos = std::abs(s.posTestRatio);
        float hgt = std::abs(s.hgtTestRatio);
        float mag = std::abs(s.magTestRatio);

        if (vel > r.velInnoMax) r.velInnoMax = vel;
        if (pos > r.posInnoMax) r.posInnoMax = pos;
        if (hgt > r.hgtInnoMax) r.hgtInnoMax = hgt;
        if (mag > r.magInnoMax) r.magInnoMax = mag;

        velSum += vel; posSum += pos; hgtSum += hgt; magSum += mag;

        if (vel > kFaultThreshold) r.velFaults++;
        if (pos > kFaultThreshold) r.posFaults++;
        if (hgt > kFaultThreshold) r.hgtFaults++;
        if (mag > kFaultThreshold) r.magFaults++;

        if (s.gpsCheckFailFlags != 0) r.gpsFaultCount++;
    }

    float n = static_cast<float>(r.totalSamples);
    r.velInnoMean = velSum / n;
    r.posInnoMean = posSum / n;
    r.hgtInnoMean = hgtSum / n;
    r.magInnoMean = magSum / n;

    // Health score: 100 - deductions for faults
    float faultPct = static_cast<float>(r.velFaults + r.posFaults + r.hgtFaults + r.magFaults) / n;
    r.healthScore = 100.0f - faultPct * 200.0f - static_cast<float>(r.gpsFaultCount) / n * 100.0f;
    if (r.healthScore < 0) r.healthScore = 0;

    return r;
}

// ============================================================================
// Dispatch analysis
// ============================================================================

QJsonObject LogAnalyzer::analyze(const QString &analysisType)
{
    QJsonObject result;
    if (!_loaded) {
        result[QStringLiteral("error")] = QStringLiteral("未加载日志文件");
        return result;
    }

    result[QStringLiteral("file")] = _filePath;
    result[QStringLiteral("type")] = analysisType;

    auto runIf = [&](const QString &key, bool cond, std::function<void()> fn) {
        if (analysisType == key || analysisType == QStringLiteral("all")) {
            // Scope dimension filter: if dimensions specified, skip non-matching keys
            if (!_scopeDimensions.isEmpty() && !_scopeDimensions.contains(key)) return;
            if (cond) fn();
        }
    };

    // ── Scope info ──
    if (_scopeStartSec >= 0 || _scopeEndSec >= 0) {
        QJsonObject scope;
        scope[QStringLiteral("startSec")] = _scopeStartSec >= 0 ? _scopeStartSec : 0;
        scope[QStringLiteral("endSec")] = _scopeEndSec >= 0 ? _scopeEndSec : -1;
        result[QStringLiteral("analysisScope")] = scope;
    }
    if (!_scopeDimensions.isEmpty()) {
        result[QStringLiteral("scopeDimensions")] = QJsonArray::fromStringList(_scopeDimensions);
    }

    runIf(QStringLiteral("vibration"), !_accelSamples.isEmpty() || !_gyroSamples.isEmpty(), [&] {
        emit analysisProgress(5);
        auto accel = _computeVibration(_accelSamples);
        emit analysisProgress(15);
        auto gyro = _computeVibration(_gyroSamples);
        emit analysisProgress(25);

        auto vibToJson = [&](const VibrationResult &r, const QString &unit) -> QJsonObject {
            QJsonObject o;
            o[QStringLiteral("samples")] = r.sampleCount;
            o[QStringLiteral("durationSec")] = static_cast<double>(r.durationSec);
            o[QStringLiteral("sampleRateHz")] = static_cast<double>(r.sampleRateHz);
            o[QStringLiteral("unit")] = unit;

            auto make3 = [](float x, float y, float z) -> QJsonObject {
                QJsonObject obj;
                obj[QStringLiteral("x")] = static_cast<double>(x);
                obj[QStringLiteral("y")] = static_cast<double>(y);
                obj[QStringLiteral("z")] = static_cast<double>(z);
                return obj;
            };
            o[QStringLiteral("rms")] = make3(r.rmsX, r.rmsY, r.rmsZ);
            o[QStringLiteral("peakFreqHz")] = make3(r.peakFreqX, r.peakFreqY, r.peakFreqZ);
            o[QStringLiteral("peakMagnitude")] = make3(r.peakMagX, r.peakMagY, r.peakMagZ);

            // P2: RPM per axis
            QJsonObject rpm;
            rpm[QStringLiteral("x")] = static_cast<int>(r.peakFreqX * 60.0f / _numBlades + 0.5f);
            rpm[QStringLiteral("y")] = static_cast<int>(r.peakFreqY * 60.0f / _numBlades + 0.5f);
            rpm[QStringLiteral("z")] = static_cast<int>(r.peakFreqZ * 60.0f / _numBlades + 0.5f);
            o[QStringLiteral("motorRpm")] = rpm;
            o[QStringLiteral("propellerBlades")] = _numBlades;

            // P5: Per-axis band energy
            auto axisEnergy = [](float lo, float mid, float hi) -> QJsonObject {
                QJsonObject obj;
                obj[QStringLiteral("low_0_50Hz")] = static_cast<double>(lo);
                obj[QStringLiteral("mid_50_200Hz")] = static_cast<double>(mid);
                obj[QStringLiteral("high_200_500Hz")] = static_cast<double>(hi);
                return obj;
            };
            QJsonObject energy;
            energy[QStringLiteral("x")] = axisEnergy(r.energyXLow, r.energyXMid, r.energyXHigh);
            energy[QStringLiteral("y")] = axisEnergy(r.energyYLow, r.energyYMid, r.energyYHigh);
            energy[QStringLiteral("z")] = axisEnergy(r.energyZLow, r.energyZMid, r.energyZHigh);
            o[QStringLiteral("bandEnergyPerAxis")] = energy;

            // Legacy: summed band energy (backward compat)
            float eSumLo = r.energyXLow + r.energyYLow + r.energyZLow;
            float eSumMid = r.energyXMid + r.energyYMid + r.energyZMid;
            float eSumHi = r.energyXHigh + r.energyYHigh + r.energyZHigh;
            QJsonObject legacy;
            legacy[QStringLiteral("low_0_50Hz")] = static_cast<double>(eSumLo);
            legacy[QStringLiteral("mid_50_200Hz")] = static_cast<double>(eSumMid);
            legacy[QStringLiteral("high_200_500Hz")] = static_cast<double>(eSumHi);
            o[QStringLiteral("bandEnergy")] = legacy;

            o[QStringLiteral("maxAbs")] = make3(r.maxX, r.maxY, r.maxZ);
            o[QStringLiteral("clipCount")] = r.clipCount;

            // P3: Optional spectrum data
            if (!r.spectrumX.isEmpty() || !r.spectrumY.isEmpty() || !r.spectrumZ.isEmpty()) {
                auto specToArray = [](const QVector<SpectrumPoint> &spec) -> QJsonArray {
                    QJsonArray arr;
                    for (const auto &p : spec) {
                        QJsonObject pt;
                        pt[QStringLiteral("f")] = static_cast<double>(p.freq);
                        pt[QStringLiteral("m")] = static_cast<double>(p.mag);
                        arr.append(pt);
                    }
                    return arr;
                };
                QJsonObject specObj;
                if (!r.spectrumX.isEmpty()) specObj[QStringLiteral("x")] = specToArray(r.spectrumX);
                if (!r.spectrumY.isEmpty()) specObj[QStringLiteral("y")] = specToArray(r.spectrumY);
                if (!r.spectrumZ.isEmpty()) specObj[QStringLiteral("z")] = specToArray(r.spectrumZ);
                o[QStringLiteral("spectrum")] = specObj;
            }

            return o;
        };
        result[QStringLiteral("accelerometer")] = vibToJson(accel, QStringLiteral("m/s²"));
        result[QStringLiteral("gyroscope")] = vibToJson(gyro, QStringLiteral("rad/s"));

        // ── Flight mode segmented vibration ──
        if (!_flightModeSamples.isEmpty()) {
            QJsonObject modeVib;
            // Collect unique mode names in order of appearance
            QVector<QString> modesSeen;
            for (const auto &m : _flightModeSamples) {
                if (!modesSeen.contains(m.modeName)) modesSeen.append(m.modeName);
            }
            for (const auto &mode : modesSeen) {
                auto filteredAccel = _filterSamplesByMode(_accelSamples, mode);
                auto filteredGyro = _filterSamplesByMode(_gyroSamples, mode);
                if (filteredAccel.size() < 100 && filteredGyro.size() < 100) continue;

                QJsonObject modeObj;
                if (!filteredAccel.isEmpty()) {
                    auto va = _computeVibration(filteredAccel);
                    modeObj[QStringLiteral("accelRms")] = QString("X=%1 Y=%2 Z=%3")
                        .arg(va.rmsX, 0, 'f', 3).arg(va.rmsY, 0, 'f', 3).arg(va.rmsZ, 0, 'f', 3);
                    modeObj[QStringLiteral("accelSamples")] = va.sampleCount;
                }
                if (!filteredGyro.isEmpty()) {
                    auto vg = _computeVibration(filteredGyro);
                    modeObj[QStringLiteral("gyroRms")] = QString("X=%1 Y=%2 Z=%3")
                        .arg(vg.rmsX, 0, 'f', 3).arg(vg.rmsY, 0, 'f', 3).arg(vg.rmsZ, 0, 'f', 3);
                    modeObj[QStringLiteral("gyroSamples")] = vg.sampleCount;
                }
                modeVib[mode] = modeObj;
            }
            if (!modeVib.isEmpty()) {
                result[QStringLiteral("vibrationByMode")] = modeVib;
            }
        }
    });

    // ── Optical flow ──
    runIf(QStringLiteral("opticalflow"), !_opticalFlowSamples.isEmpty(), [&] {
        emit analysisProgress(30);
        auto flow = _computeOpticalFlowStats();
        QJsonObject o;
        o[QStringLiteral("samples")] = flow.totalSamples;
        o[QStringLiteral("durationSec")] = static_cast<double>(flow.durationSec);
        o[QStringLiteral("qualityMin")] = static_cast<double>(flow.qualityMin);
        o[QStringLiteral("qualityMax")] = static_cast<double>(flow.qualityMax);
        o[QStringLiteral("qualityMean")] = static_cast<double>(flow.qualityMean);
        o[QStringLiteral("lowQualityCount")] = flow.lowQualityCount;
        o[QStringLiteral("flowMagMean")] = static_cast<double>(flow.flowMagMean);
        o[QStringLiteral("flowMagMax")] = static_cast<double>(flow.flowMagMax);
        o[QStringLiteral("groundDistMin")] = static_cast<double>(flow.groundDistMin);
        o[QStringLiteral("groundDistMax")] = static_cast<double>(flow.groundDistMax);
        o[QStringLiteral("groundDistMean")] = static_cast<double>(flow.groundDistMean);
        result[QStringLiteral("opticalFlow")] = o;
    });

    runIf(QStringLiteral("gps"), !_gpsSamples.isEmpty(), [&] {
        emit analysisProgress(35);
        auto gps = _computeGpsStats();
        QJsonObject o;
        o[QStringLiteral("samples")] = gps.totalSamples;
        o[QStringLiteral("durationSec")] = static_cast<double>(gps.durationSec);
        o[QStringLiteral("hdopMin")] = static_cast<double>(gps.hdopMin);
        o[QStringLiteral("hdopMax")] = static_cast<double>(gps.hdopMax);
        o[QStringLiteral("hdopMean")] = static_cast<double>(gps.hdopMean);
        o[QStringLiteral("vdopMin")] = static_cast<double>(gps.vdopMin);
        o[QStringLiteral("vdopMax")] = static_cast<double>(gps.vdopMax);
        o[QStringLiteral("vdopMean")] = static_cast<double>(gps.vdopMean);
        o[QStringLiteral("satMin")] = gps.satMin;
        o[QStringLiteral("satMax")] = gps.satMax;
        o[QStringLiteral("satMean")] = static_cast<double>(gps.satMean);
        o[QStringLiteral("noFixPct")] = static_cast<double>(gps.noFixPct);
        o[QStringLiteral("fix2dPct")] = static_cast<double>(gps.fix2dPct);
        o[QStringLiteral("fix3dPct")] = static_cast<double>(gps.fix3dPct);
        o[QStringLiteral("fixRtkPct")] = static_cast<double>(gps.fixRtkPct);
        o[QStringLiteral("positionJumps")] = gps.positionJumps;
        o[QStringLiteral("maxHdopSpike")] = static_cast<double>(gps.maxHdopSpike);
        result[QStringLiteral("gps")] = o;
        emit analysisProgress(45);
    });

    runIf(QStringLiteral("battery"), !_batterySamples.isEmpty(), [&] {
        emit analysisProgress(55);
        auto bat = _computeBatteryStats();
        QJsonObject o;
        o[QStringLiteral("samples")] = bat.totalSamples;
        o[QStringLiteral("durationSec")] = static_cast<double>(bat.durationSec);
        o[QStringLiteral("voltageMin")] = static_cast<double>(bat.voltageMin);
        o[QStringLiteral("voltageMax")] = static_cast<double>(bat.voltageMax);
        o[QStringLiteral("voltageMean")] = static_cast<double>(bat.voltageMean);
        o[QStringLiteral("voltageSag")] = static_cast<double>(bat.voltageSag);
        o[QStringLiteral("currentMin")] = static_cast<double>(bat.currentMin);
        o[QStringLiteral("currentMax")] = static_cast<double>(bat.currentMax);
        o[QStringLiteral("currentMean")] = static_cast<double>(bat.currentMean);
        o[QStringLiteral("capacityConsumedMah")] = static_cast<double>(bat.capacityConsumedMah);
        o[QStringLiteral("capacityPctStart")] = static_cast<double>(bat.capacityPctStart);
        o[QStringLiteral("capacityPctEnd")] = static_cast<double>(bat.capacityPctEnd);
        o[QStringLiteral("avgDischargeRateC")] = static_cast<double>(bat.avgDischargeRateC);
        o[QStringLiteral("tempMin")] = static_cast<double>(bat.tempMin);
        o[QStringLiteral("tempMax")] = static_cast<double>(bat.tempMax);
        o[QStringLiteral("energyWh")] = static_cast<double>(bat.energyWh);
        result[QStringLiteral("battery")] = o;
        emit analysisProgress(65);
    });

    runIf(QStringLiteral("ekf"), !_ekfSamples.isEmpty(), [&] {
        emit analysisProgress(75);
        auto ekf = _computeEkfStats();
        QJsonObject o;
        o[QStringLiteral("samples")] = ekf.totalSamples;
        o[QStringLiteral("durationSec")] = static_cast<double>(ekf.durationSec);
        o[QStringLiteral("velInnoMax")] = static_cast<double>(ekf.velInnoMax);
        o[QStringLiteral("velInnoMean")] = static_cast<double>(ekf.velInnoMean);
        o[QStringLiteral("posInnoMax")] = static_cast<double>(ekf.posInnoMax);
        o[QStringLiteral("posInnoMean")] = static_cast<double>(ekf.posInnoMean);
        o[QStringLiteral("hgtInnoMax")] = static_cast<double>(ekf.hgtInnoMax);
        o[QStringLiteral("hgtInnoMean")] = static_cast<double>(ekf.hgtInnoMean);
        o[QStringLiteral("magInnoMax")] = static_cast<double>(ekf.magInnoMax);
        o[QStringLiteral("magInnoMean")] = static_cast<double>(ekf.magInnoMean);
        o[QStringLiteral("velFaults")] = ekf.velFaults;
        o[QStringLiteral("posFaults")] = ekf.posFaults;
        o[QStringLiteral("hgtFaults")] = ekf.hgtFaults;
        o[QStringLiteral("magFaults")] = ekf.magFaults;
        o[QStringLiteral("gpsFaultCount")] = ekf.gpsFaultCount;
        o[QStringLiteral("healthScore")] = static_cast<double>(ekf.healthScore);
        result[QStringLiteral("ekf")] = o;
        emit analysisProgress(85);
    });

    emit analysisProgress(100);
    return result;
}

// ============================================================================
// Report generation
// ============================================================================

static QString _severity(float val, float good, float warn)
{
    if (val <= good) return QStringLiteral("✅ 正常");
    if (val <= warn) return QStringLiteral("⚠ 关注");
    return QStringLiteral("❌ 异常");
}

QString LogAnalyzer::generateReport(const QJsonObject &results)
{
    if (results.contains(QStringLiteral("error"))) {
        return QStringLiteral("分析出错: ") + results.value(QStringLiteral("error")).toString();
    }

    QString type = results.value(QStringLiteral("type")).toString();
    QString report;
    report += QStringLiteral("## 飞控日志分析报告\n");
    report += QStringLiteral("**文件**: ") + results.value(QStringLiteral("file")).toString() + QStringLiteral("\n");

    // ── Analysis scope ──
    if (results.contains(QStringLiteral("analysisScope"))) {
        auto scope = results.value(QStringLiteral("analysisScope")).toObject();
        double startSec = scope.value(QStringLiteral("startSec")).toDouble();
        double endSec = scope.value(QStringLiteral("endSec")).toDouble();
        report += QStringLiteral("**分析范围**: %1s ~ %2s\n")
            .arg(startSec, 0, 'f', 1)
            .arg(endSec < 0 ? QStringLiteral("结尾") : QString::number(endSec, 'f', 1));
    }
    if (results.contains(QStringLiteral("scopeDimensions"))) {
        auto dims = results.value(QStringLiteral("scopeDimensions")).toArray();
        QStringList dimNames;
        for (const auto &d : dims) dimNames.append(d.toString());
        report += QStringLiteral("**分析维度**: %1\n").arg(dimNames.join(", "));
    }
    report += QStringLiteral("\n");

    // ── Vibration ──
    if (results.contains(QStringLiteral("accelerometer"))) {
        report += QStringLiteral("### 📊 震动分析 (加速度计)\n\n");
        auto data = results.value(QStringLiteral("accelerometer")).toObject();
        auto rms = data.value(QStringLiteral("rms")).toObject();
        float rx = static_cast<float>(rms.value(QStringLiteral("x")).toDouble());
        float ry = static_cast<float>(rms.value(QStringLiteral("y")).toDouble());
        float rz = static_cast<float>(rms.value(QStringLiteral("z")).toDouble());

        report += QStringLiteral("- 采样: %1 | 时长: %2s | 频率: %3Hz\n")
            .arg(data.value(QStringLiteral("samples")).toInt())
            .arg(data.value(QStringLiteral("durationSec")).toDouble(), 0, 'f', 1)
            .arg(data.value(QStringLiteral("sampleRateHz")).toDouble(), 0, 'f', 0);
        report += QStringLiteral("- RMS: X=%1 Y=%2 Z=%3 m/s²\n")
            .arg(rx, 0, 'f', 3).arg(ry, 0, 'f', 3).arg(rz, 0, 'f', 3);
        report += QStringLiteral("- 评估: X=%1 Y=%2 Z=%3\n")
            .arg(_severity(rx, _accelGoodThreshold, _accelWarnThreshold))
            .arg(_severity(ry, _accelGoodThreshold, _accelWarnThreshold))
            .arg(_severity(rz, _accelGoodThreshold, _accelWarnThreshold));

        auto pf = data.value(QStringLiteral("peakFreqHz")).toObject();
        float pfx = static_cast<float>(pf.value(QStringLiteral("x")).toDouble());
        float pfy = static_cast<float>(pf.value(QStringLiteral("y")).toDouble());
        float pfz = static_cast<float>(pf.value(QStringLiteral("z")).toDouble());
        report += QStringLiteral("- 主频: X=%1Hz Y=%2Hz Z=%3Hz\n")
            .arg(pfx, 0, 'f', 1).arg(pfy, 0, 'f', 1).arg(pfz, 0, 'f', 1);

        // P2: RPM
        auto rpmObj = data.value(QStringLiteral("motorRpm")).toObject();
        report += QStringLiteral("- 电机转速: X=%1 Y=%2 Z=%3 RPM (%4 桨叶)\n")
            .arg(rpmObj.value(QStringLiteral("x")).toInt())
            .arg(rpmObj.value(QStringLiteral("y")).toInt())
            .arg(rpmObj.value(QStringLiteral("z")).toInt())
            .arg(data.value(QStringLiteral("propellerBlades")).toInt());

        // P5: Per-axis band energy
        auto bandPerAxis = data.value(QStringLiteral("bandEnergyPerAxis")).toObject();
        QStringList axes = {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")};
        for (const auto &axis : axes) {
            auto b = bandPerAxis.value(axis).toObject();
            float eLo = static_cast<float>(b.value(QStringLiteral("low_0_50Hz")).toDouble());
            float eMid = static_cast<float>(b.value(QStringLiteral("mid_50_200Hz")).toDouble());
            float eHi = static_cast<float>(b.value(QStringLiteral("high_200_500Hz")).toDouble());
            float eTot = eLo + eMid + eHi;
            if (eTot > 0) {
                report += QStringLiteral("- 频带能量 %1: 0-50Hz=%2% 50-200Hz=%3% 200-500Hz=%4%\n")
                    .arg(axis.toUpper())
                    .arg(eLo / eTot * 100.0f, 0, 'f', 0)
                    .arg(eMid / eTot * 100.0f, 0, 'f', 0)
                    .arg(eHi / eTot * 100.0f, 0, 'f', 0);
            }
        }

        int clips = data.value(QStringLiteral("clipCount")).toInt();
        if (clips > 0) report += QStringLiteral("- ⚠ 削波: %1 次 (传感器饱和)\n").arg(clips);
        report += QStringLiteral("\n");
    }

    // ── Gyroscope (P0) ──
    if (results.contains(QStringLiteral("gyroscope"))) {
        report += QStringLiteral("### 📊 震动分析 (陀螺仪)\n\n");
        auto data = results.value(QStringLiteral("gyroscope")).toObject();
        auto rms = data.value(QStringLiteral("rms")).toObject();
        float rx = static_cast<float>(rms.value(QStringLiteral("x")).toDouble());
        float ry = static_cast<float>(rms.value(QStringLiteral("y")).toDouble());
        float rz = static_cast<float>(rms.value(QStringLiteral("z")).toDouble());

        report += QStringLiteral("- 采样: %1 | 时长: %2s | 频率: %3Hz\n")
            .arg(data.value(QStringLiteral("samples")).toInt())
            .arg(data.value(QStringLiteral("durationSec")).toDouble(), 0, 'f', 1)
            .arg(data.value(QStringLiteral("sampleRateHz")).toDouble(), 0, 'f', 0);
        report += QStringLiteral("- RMS: X=%1 Y=%2 Z=%3 rad/s\n")
            .arg(rx, 0, 'f', 3).arg(ry, 0, 'f', 3).arg(rz, 0, 'f', 3);
        report += QStringLiteral("- 评估: X=%1 Y=%2 Z=%3\n")
            .arg(_severity(rx, _gyroGoodThreshold, _gyroWarnThreshold))
            .arg(_severity(ry, _gyroGoodThreshold, _gyroWarnThreshold))
            .arg(_severity(rz, _gyroGoodThreshold, _gyroWarnThreshold));

        auto pf = data.value(QStringLiteral("peakFreqHz")).toObject();
        report += QStringLiteral("- 主频: X=%1Hz Y=%2Hz Z=%3Hz\n")
            .arg(pf.value(QStringLiteral("x")).toDouble(), 0, 'f', 1)
            .arg(pf.value(QStringLiteral("y")).toDouble(), 0, 'f', 1)
            .arg(pf.value(QStringLiteral("z")).toDouble(), 0, 'f', 1);

        // P5: Per-axis band energy
        auto bandPerAxis = data.value(QStringLiteral("bandEnergyPerAxis")).toObject();
        QStringList axes = {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")};
        for (const auto &axis : axes) {
            auto b = bandPerAxis.value(axis).toObject();
            float eLo = static_cast<float>(b.value(QStringLiteral("low_0_50Hz")).toDouble());
            float eMid = static_cast<float>(b.value(QStringLiteral("mid_50_200Hz")).toDouble());
            float eHi = static_cast<float>(b.value(QStringLiteral("high_200_500Hz")).toDouble());
            float eTot = eLo + eMid + eHi;
            if (eTot > 0) {
                report += QStringLiteral("- 频带能量 %1: 0-50Hz=%2% 50-200Hz=%3% 200-500Hz=%4%\n")
                    .arg(axis.toUpper())
                    .arg(eLo / eTot * 100.0f, 0, 'f', 0)
                    .arg(eMid / eTot * 100.0f, 0, 'f', 0)
                    .arg(eHi / eTot * 100.0f, 0, 'f', 0);
            }
        }

        int clips = data.value(QStringLiteral("clipCount")).toInt();
        if (clips > 0) report += QStringLiteral("- ⚠ 削波: %1 次 (传感器饱和)\n").arg(clips);
        report += QStringLiteral("\n");
    }

    // ── Flight mode segmented vibration ──
    if (results.contains(QStringLiteral("vibrationByMode"))) {
        report += QStringLiteral("### ✈️ 按飞行模式分段震动\n\n");
        auto modeVib = results.value(QStringLiteral("vibrationByMode")).toObject();
        for (auto it = modeVib.begin(); it != modeVib.end(); ++it) {
            auto m = it.value().toObject();
            report += QStringLiteral("**%1**\n").arg(it.key());
            if (m.contains(QStringLiteral("accelRms")))
                report += QStringLiteral("- 加速度 RMS: %1 m/s² (%2 采样)\n")
                    .arg(m.value(QStringLiteral("accelRms")).toString())
                    .arg(m.value(QStringLiteral("accelSamples")).toInt());
            if (m.contains(QStringLiteral("gyroRms")))
                report += QStringLiteral("- 陀螺仪 RMS: %1 rad/s (%2 采样)\n")
                    .arg(m.value(QStringLiteral("gyroRms")).toString())
                    .arg(m.value(QStringLiteral("gyroSamples")).toInt());
        }
        report += QStringLiteral("\n");
    }

    // ── Optical flow ──
    if (results.contains(QStringLiteral("opticalFlow"))) {
        report += QStringLiteral("### 👁️ 光流健康 (GPS-Denied)\n\n");
        auto f = results.value(QStringLiteral("opticalFlow")).toObject();
        report += QStringLiteral("- 采样: %1 | 时长: %2s\n")
            .arg(f.value(QStringLiteral("samples")).toInt())
            .arg(f.value(QStringLiteral("durationSec")).toDouble(), 0, 'f', 1);
        report += QStringLiteral("- 质量: 最小=%1 最大=%2 均值=%3\n")
            .arg(f.value(QStringLiteral("qualityMin")).toDouble(), 0, 'f', 0)
            .arg(f.value(QStringLiteral("qualityMax")).toDouble(), 0, 'f', 0)
            .arg(f.value(QStringLiteral("qualityMean")).toDouble(), 0, 'f', 1);
        int lowQ = f.value(QStringLiteral("lowQualityCount")).toInt();
        if (lowQ > 0) report += QStringLiteral("- ⚠ 低质量采样 (Q<100): %1 次\n").arg(lowQ);
        report += QStringLiteral("- 流量幅值: 均值=%1 最大=%2 rad/s\n")
            .arg(f.value(QStringLiteral("flowMagMean")).toDouble(), 0, 'f', 4)
            .arg(f.value(QStringLiteral("flowMagMax")).toDouble(), 0, 'f', 4);
        float distMean = static_cast<float>(f.value(QStringLiteral("groundDistMean")).toDouble());
        if (distMean > 0) {
            report += QStringLiteral("- 地面距离: %1~%2m (均值 %3m)\n")
                .arg(f.value(QStringLiteral("groundDistMin")).toDouble(), 0, 'f', 2)
                .arg(f.value(QStringLiteral("groundDistMax")).toDouble(), 0, 'f', 2)
                .arg(distMean, 0, 'f', 2);
        }
        float qMean = static_cast<float>(f.value(QStringLiteral("qualityMean")).toDouble());
        report += QStringLiteral("- 质量评估: %1\n")
            .arg(_severity(255.0f - qMean, 255.0f - 200.0f, 255.0f - 100.0f));
        report += QStringLiteral("\n");
    }

    // ── GPS ──
    if (results.contains(QStringLiteral("gps"))) {
        auto g = results.value(QStringLiteral("gps")).toObject();
        report += QStringLiteral("### 🛰 GPS 健康\n\n");
        report += QStringLiteral("- 采样: %1 | 时长: %2s\n")
            .arg(g.value(QStringLiteral("samples")).toInt())
            .arg(g.value(QStringLiteral("durationSec")).toDouble(), 0, 'f', 0);
        report += QStringLiteral("- HDOP: 最小=%1 最大=%2 均值=%3\n")
            .arg(g.value(QStringLiteral("hdopMin")).toDouble(), 0, 'f', 2)
            .arg(g.value(QStringLiteral("hdopMax")).toDouble(), 0, 'f', 2)
            .arg(g.value(QStringLiteral("hdopMean")).toDouble(), 0, 'f', 2);
        report += QStringLiteral("- 卫星数: %1~%2 (均值 %3)\n")
            .arg(g.value(QStringLiteral("satMin")).toInt())
            .arg(g.value(QStringLiteral("satMax")).toInt())
            .arg(g.value(QStringLiteral("satMean")).toDouble(), 0, 'f', 1);
        report += QStringLiteral("- 定位: 无定位=%1% 2D=%2% 3D=%3% RTK=%4%\n")
            .arg(g.value(QStringLiteral("noFixPct")).toDouble(), 0, 'f', 1)
            .arg(g.value(QStringLiteral("fix2dPct")).toDouble(), 0, 'f', 1)
            .arg(g.value(QStringLiteral("fix3dPct")).toDouble(), 0, 'f', 1)
            .arg(g.value(QStringLiteral("fixRtkPct")).toDouble(), 0, 'f', 1);

        int jumps = g.value(QStringLiteral("positionJumps")).toInt();
        float hdopSpike = static_cast<float>(g.value(QStringLiteral("maxHdopSpike")).toDouble());
        float hdopMean = static_cast<float>(g.value(QStringLiteral("hdopMean")).toDouble());

        if (jumps > 0) report += QStringLiteral("- ⚠ 位置跳变: %1 次\n").arg(jumps);
        if (hdopSpike > 1.0f) report += QStringLiteral("- ⚠ HDOP 尖峰: +%1 (瞬时精度恶化)\n").arg(hdopSpike, 0, 'f', 2);
        report += QStringLiteral("- HDOP评估: %1\n").arg(_severity(hdopMean, 0.8f, 1.5f));

        auto gpsVdopMean = static_cast<float>(g.value(QStringLiteral("vdopMean")).toDouble());
        if (gpsVdopMean > 2.0f) report += QStringLiteral("- ⚠ 垂直精度 (VDOP=%1) 较差，影响定高\n").arg(gpsVdopMean, 0, 'f', 2);
        report += QStringLiteral("\n");
    }

    // ── Battery ──
    if (results.contains(QStringLiteral("battery"))) {
        auto b = results.value(QStringLiteral("battery")).toObject();
        report += QStringLiteral("### 🔋 电池性能\n\n");
        report += QStringLiteral("- 采样: %1 | 时长: %2s\n")
            .arg(b.value(QStringLiteral("samples")).toInt())
            .arg(b.value(QStringLiteral("durationSec")).toDouble(), 0, 'f', 0);
        report += QStringLiteral("- 电压: %1~%2V (均值 %3V) 压降: %4V\n")
            .arg(b.value(QStringLiteral("voltageMin")).toDouble(), 0, 'f', 2)
            .arg(b.value(QStringLiteral("voltageMax")).toDouble(), 0, 'f', 2)
            .arg(b.value(QStringLiteral("voltageMean")).toDouble(), 0, 'f', 2)
            .arg(b.value(QStringLiteral("voltageSag")).toDouble(), 0, 'f', 2);
        report += QStringLiteral("- 电流: %1~%2A (均值 %3A)\n")
            .arg(b.value(QStringLiteral("currentMin")).toDouble(), 0, 'f', 1)
            .arg(b.value(QStringLiteral("currentMax")).toDouble(), 0, 'f', 1)
            .arg(b.value(QStringLiteral("currentMean")).toDouble(), 0, 'f', 1);
        report += QStringLiteral("- 电量: %1%→%2% | 消耗: %3mAh / %4Wh\n")
            .arg(b.value(QStringLiteral("capacityPctStart")).toDouble(), 0, 'f', 1)
            .arg(b.value(QStringLiteral("capacityPctEnd")).toDouble(), 0, 'f', 1)
            .arg(b.value(QStringLiteral("capacityConsumedMah")).toDouble(), 0, 'f', 0)
            .arg(b.value(QStringLiteral("energyWh")).toDouble(), 0, 'f', 2);
        report += QStringLiteral("- 放电倍率: %1 C | 温度: %2~%3°C\n")
            .arg(b.value(QStringLiteral("avgDischargeRateC")).toDouble(), 0, 'f', 2)
            .arg(b.value(QStringLiteral("tempMin")).toDouble(), 0, 'f', 0)
            .arg(b.value(QStringLiteral("tempMax")).toDouble(), 0, 'f', 0);

        float vSag = static_cast<float>(b.value(QStringLiteral("voltageSag")).toDouble());
        report += QStringLiteral("- 压降评估: %1\n").arg(_severity(vSag, 1.0f, 2.5f));
        report += QStringLiteral("\n");
    }

    // ── EKF ──
    if (results.contains(QStringLiteral("ekf"))) {
        auto e = results.value(QStringLiteral("ekf")).toObject();
        report += QStringLiteral("### 🧠 EKF 滤波器健康\n\n");
        report += QStringLiteral("- 采样: %1 | 时长: %2s\n")
            .arg(e.value(QStringLiteral("samples")).toInt())
            .arg(e.value(QStringLiteral("durationSec")).toDouble(), 0, 'f', 0);

        report += QStringLiteral("- 速度创新: max=%1 mean=%2 | 异常: %3次\n")
            .arg(e.value(QStringLiteral("velInnoMax")).toDouble(), 0, 'f', 3)
            .arg(e.value(QStringLiteral("velInnoMean")).toDouble(), 0, 'f', 3)
            .arg(e.value(QStringLiteral("velFaults")).toInt());
        report += QStringLiteral("- 位置创新: max=%1 mean=%2 | 异常: %3次\n")
            .arg(e.value(QStringLiteral("posInnoMax")).toDouble(), 0, 'f', 3)
            .arg(e.value(QStringLiteral("posInnoMean")).toDouble(), 0, 'f', 3)
            .arg(e.value(QStringLiteral("posFaults")).toInt());
        report += QStringLiteral("- 高度创新: max=%1 mean=%2 | 异常: %3次\n")
            .arg(e.value(QStringLiteral("hgtInnoMax")).toDouble(), 0, 'f', 3)
            .arg(e.value(QStringLiteral("hgtInnoMean")).toDouble(), 0, 'f', 3)
            .arg(e.value(QStringLiteral("hgtFaults")).toInt());
        report += QStringLiteral("- 磁力计创新: max=%1 mean=%2 | 异常: %3次\n")
            .arg(e.value(QStringLiteral("magInnoMax")).toDouble(), 0, 'f', 3)
            .arg(e.value(QStringLiteral("magInnoMean")).toDouble(), 0, 'f', 3)
            .arg(e.value(QStringLiteral("magFaults")).toInt());

        int gpsFaults = e.value(QStringLiteral("gpsFaultCount")).toInt();
        if (gpsFaults > 0) report += QStringLiteral("- ⚠ GPS 质量触发 EKF 保护: %1 次\n").arg(gpsFaults);

        float score = static_cast<float>(e.value(QStringLiteral("healthScore")).toDouble());
        report += QStringLiteral("- 综合健康评分: %1% (%2)\n")
            .arg(score, 0, 'f', 0)
            .arg(score >= 90 ? QStringLiteral("✅ 优秀") : score >= 70 ? QStringLiteral("⚠ 一般") : QStringLiteral("❌ 差"));
        report += QStringLiteral("\n");
    }

    // ── Summary ──
    report += QStringLiteral("---\n");
    report += QStringLiteral("请综合分析以上数据，识别潜在问题，给出优化建议和下一步行动计划。\n");

    // Persist report
    _saveReport(report);
    if (!_lastSavedReportPath.isEmpty()) {
        report += QStringLiteral("\n📁 报告已保存: ") + _lastSavedReportPath + QStringLiteral("\n");
    }

    return report;
}
