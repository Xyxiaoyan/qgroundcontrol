#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

#include <complex>
#include <cstdint>

class LogAnalyzer : public QObject
{
    Q_OBJECT
public:
    // ── Common sample type for multi-axis sensor data ──
    struct SensorSample {
        uint64_t timestampUs = 0;
        float x = 0, y = 0, z = 0;
    };

    struct SpectrumPoint {
        float freq = 0;
        float mag = 0;
    };

    struct VibrationResult {
        float rmsX = 0, rmsY = 0, rmsZ = 0;
        float peakFreqX = 0, peakFreqY = 0, peakFreqZ = 0;
        float peakMagX = 0, peakMagY = 0, peakMagZ = 0;
        int clipCount = 0;
        float maxX = 0, maxY = 0, maxZ = 0;
        float meanX = 0, meanY = 0, meanZ = 0;
        int sampleCount = 0;
        float durationSec = 0;
        float sampleRateHz = 0;
        // Per-axis band energy (populated by FFT)
        float energyXLow = 0, energyXMid = 0, energyXHigh = 0;
        float energyYLow = 0, energyYMid = 0, energyYHigh = 0;
        float energyZLow = 0, energyZMid = 0, energyZHigh = 0;
        // Optional spectrum data
        QVector<SpectrumPoint> spectrumX, spectrumY, spectrumZ;
    };

    // ── GPS ──
    struct GpsSample {
        uint64_t timestampUs = 0;
        double lat = 0, lon = 0;
        float alt = 0, hdop = 99, vdop = 99;
        int satellitesUsed = 0, fixType = 0;
    };

    struct GpsResult {
        int totalSamples = 0;
        float durationSec = 0;
        float hdopMin = 99, hdopMax = 0, hdopMean = 0;
        float vdopMin = 99, vdopMax = 0, vdopMean = 0;
        int satMin = 99, satMax = 0;
        float satMean = 0;
        int fixTypeDist[8] = {}; // fixType 0-7
        float noFixPct = 0, fix2dPct = 0, fix3dPct = 0, fixRtkPct = 0;
        int positionJumps = 0;
        float maxHdopSpike = 0;
        bool hasData = false;
    };

    // ── Battery ──
    struct BatterySample {
        uint64_t timestampUs = 0;
        float voltage = 0, current = 0;
        float remaining = 0, temperature = 0;
    };

    struct BatteryResult {
        int totalSamples = 0;
        float durationSec = 0;
        float voltageMin = 99, voltageMax = 0, voltageMean = 0;
        float voltageSag = 0;        // max - min under load
        float currentMin = 0, currentMax = 0, currentMean = 0;
        float capacityConsumedMah = 0; // integrated current
        float capacityPctStart = 0, capacityPctEnd = 0;
        float avgDischargeRateC = 0;  // in C-rate
        float tempMin = 0, tempMax = 0;
        float energyWh = 0;
        bool hasData = false;
    };

    // ── PID / Rate controller ──
    struct PidSample {
        uint64_t timestampUs = 0;
        float rollSetpoint = 0, rollActual = 0;
        float pitchSetpoint = 0, pitchActual = 0;
        float yawSetpoint = 0, yawActual = 0;
    };

    struct PidAxisResult {
        float trackingErrorRms = 0;
        float maxError = 0;
        float overshootPct = 0;
        float settlingTimeSec = 0;
        float steadyStateError = 0;
        int oscillationCount = 0;
    };

    struct PidResult {
        int totalSamples = 0;
        float durationSec = 0;
        PidAxisResult roll, pitch, yaw;
        bool hasData = false;
    };

    // ── Flight mode ──
    struct FlightModeSample {
        uint64_t timestampUs = 0;
        int navState = 0;          // PX4: nav_state, ArduPilot: custom mode
        QString modeName;          // Resolved name: "Stabilized", "Loiter", etc.
    };

    // ── Optical flow ──
    struct OpticalFlowSample {
        uint64_t timestampUs = 0;
        float flowX = 0, flowY = 0;      // rad/s or integrated px
        float quality = 0;                // 0-255
        float groundDistanceM = 0;
    };

    struct OpticalFlowResult {
        int totalSamples = 0;
        float durationSec = 0;
        float qualityMin = 255, qualityMax = 0, qualityMean = 0;
        int lowQualityCount = 0;          // quality < 100
        float flowMagMean = 0;            // sqrt(flowX² + flowY²) mean
        float flowMagMax = 0;
        float groundDistMin = 0, groundDistMax = 0, groundDistMean = 0;
        bool hasData = false;
    };

    // ── EKF ──
    struct EkfSample {
        uint64_t timestampUs = 0;
        float velTestRatio = 0, posTestRatio = 0, hgtTestRatio = 0;
        float magTestRatio = 0, haglTestRatio = 0, tasTestRatio = 0;
        int gpsCheckFailFlags = 0;
    };

    struct EkfResult {
        int totalSamples = 0;
        float durationSec = 0;
        float velInnoMax = 0, velInnoMean = 0;
        float posInnoMax = 0, posInnoMean = 0;
        float hgtInnoMax = 0, hgtInnoMean = 0;
        float magInnoMax = 0, magInnoMean = 0;
        int velFaults = 0, posFaults = 0, hgtFaults = 0, magFaults = 0;
        int gpsFaultCount = 0;
        float healthScore = 100; // 100 = perfect
        bool hasData = false;
    };

    explicit LogAnalyzer(QObject *parent = nullptr);

    Q_INVOKABLE bool loadLog(const QString &filePath);
    Q_INVOKABLE QJsonObject analyze(const QString &analysisType);
    Q_INVOKABLE QString generateReport(const QJsonObject &results);

    // ── Configurable parameters ──
    Q_INVOKABLE void setNumBlades(int blades) { _numBlades = qBound(1, blades, 8); }
    Q_INVOKABLE int numBlades() const { return _numBlades; }

    Q_INVOKABLE void setIncludeSpectrum(bool include) { _includeSpectrum = include; }
    Q_INVOKABLE bool includeSpectrum() const { return _includeSpectrum; }

    Q_INVOKABLE void setVibrationThresholds(float accelGood, float accelWarn,
                                            float gyroGood, float gyroWarn);

    // ── Analysis scope ──
    Q_INVOKABLE void setAnalysisTimeRange(double startSec, double endSec);
    Q_INVOKABLE void setAnalysisDimensions(const QStringList &dims);

    // ── Time-series data for visualization ──
    struct TimeSeriesData {
        QJsonArray accelSeries;
        QJsonArray gyroSeries;
        QJsonArray gpsSeries;
        QJsonArray batterySeries;
        QJsonArray ekfSeries;
        QJsonArray pidSeries;
        QJsonArray opticalFlowSeries;
        QJsonArray flightModeSeries;
    };

    /// Serialize raw samples to QJsonArrays for QML chart rendering.
    /// Timestamps are seconds-relative-to-first-sample, subsampled to maxPoints.
    TimeSeriesData timeSeriesJson(int maxPoints = 2000) const;

    QString filePath() const { return _filePath; }
    QString errorString() const { return _errorString; }
    bool isLoaded() const { return _loaded; }
    QString lastSavedReportPath() const;

signals:
    void analysisProgress(int percent);
    void analysisError(const QString &error);

private:
    bool _extractImuData();
    bool _extractGpsData();
    bool _extractBatteryData();
    bool _extractPidData();
    bool _extractEkfData();
    bool _extractFlightModeData();
    bool _extractOpticalFlowData();
    void _extractDataFlashData();
    void _saveReport(const QString &report);

    VibrationResult _computeVibration(const QVector<SensorSample> &samples);
    GpsResult _computeGpsStats();
    BatteryResult _computeBatteryStats();
    PidResult _computePidStats();
    EkfResult _computeEkfStats();
    OpticalFlowResult _computeOpticalFlowStats();

    // Flight mode helpers
    static QString _px4ModeName(int navState);
    static QString _arduModeName(int customMode);
    QVector<SensorSample> _filterSamplesByMode(const QVector<SensorSample> &samples,
                                                const QString &modeName) const;

    static void _fft(std::vector<std::complex<float>> &data);
    static int _findMaxRmsSegment(const QVector<SensorSample> &samples, int segmentSize);

    QString _filePath;
    QByteArray _fileData;
    QString _errorString;
    QString _lastSavedReportPath;
    bool _loaded = false;

    QVector<SensorSample> _accelSamples;
    QVector<SensorSample> _gyroSamples;
    QVector<GpsSample> _gpsSamples;
    QVector<BatterySample> _batterySamples;
    QVector<PidSample> _pidSamples;
    QVector<EkfSample> _ekfSamples;
    QVector<FlightModeSample> _flightModeSamples;
    QVector<OpticalFlowSample> _opticalFlowSamples;

    // ── Configurable parameters ──
    int _numBlades = 2;
    bool _includeSpectrum = false;
    float _accelGoodThreshold = 0.5f;
    float _accelWarnThreshold = 1.5f;
    float _gyroGoodThreshold = 0.1f;
    float _gyroWarnThreshold = 0.3f;

    // ── Analysis scope ──
    double _scopeStartSec = -1;    // -1 = no filter
    double _scopeEndSec = -1;
    QStringList _scopeDimensions;  // empty = all
};
