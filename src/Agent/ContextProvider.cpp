#include "ContextProvider.h"
#include "AgentMapProvider.h"
#include "CameraProvider.h"
#include "Vehicle.h"
#include "Fact.h"
#include "ParameterManager.h"
#include "VehicleGPSFactGroup.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "BatteryFactGroupListModel.h"

QGC_LOGGING_CATEGORY(ContextLog, "qgc.agent.ctx")

ContextProvider::ContextProvider(QObject *parent)
    : QObject(parent)
    , _map(new AgentMapProvider(this))
    , _cam(new CameraProvider(this))
{
    _trendTimer.start();
}

void ContextProvider::setGeocodeApiKey(const QString &key)
{
    if (_map) _map->setGeocodeApiKey(key);
}

QString ContextProvider::buildVehicleContext()
{
    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        return QStringLiteral("无飞行器连接。");
    }

    QString ctx;
    ctx += QStringLiteral("## 飞行器状态\n");
    ctx += _collectBasicInfo(v);
    ctx += QStringLiteral("\n## 姿态与运动\n");
    ctx += _collectAttitudeData(v);
    ctx += QStringLiteral("\n## 电池\n");
    ctx += _collectBatteryData(v);
    ctx += QStringLiteral("\n## GPS\n");
    ctx += _collectGpsData(v);
    ctx += QStringLiteral("\n## 健康与通讯\n");
    ctx += _collectHealthData(v);
    ctx += _collectParameterSummary(v);
    ctx += _map->buildMapContext(v);
    ctx += _cam->buildCameraContext();
    ctx += _buildTrendContext(v);

    return ctx;
}

QString ContextProvider::_buildTrendContext(Vehicle *v)
{
    QString s;
    float batPct = -1, altRel = 0, heading = 0;
    int gpsCount = -1, gpsLock = -1;

    // Collect current values
    auto *batteries = v->batteries();
    if (batteries && batteries->count() > 0) {
        auto *batGroup = dynamic_cast<BatteryFactGroup *>(batteries->get(0));
        if (batGroup && batGroup->percentRemaining()) {
            batPct = static_cast<float>(batGroup->percentRemaining()->rawValue().toDouble());
        }
    }
    altRel = _safeFactValue(v->altitudeRelative());
    heading = _safeFactValue(v->heading());
    auto *gps = dynamic_cast<VehicleGPSFactGroup *>(v->gpsFactGroup());
    if (gps) {
        gpsCount = _safeFactInt(gps->count(), -1);
        gpsLock = _safeFactInt(gps->lock(), -1);
    }

    _sampleCount++;
    if (_sampleCount <= 1) {
        // First sample: set baseline
        _prevBatteryPct = batPct;
        _prevAltRel = altRel;
        _prevHeading = heading;
        _prevGpsCount = gpsCount;
        _prevGpsLock = gpsLock;
        _trendTimer.restart();
        return s;
    }

    qint64 elapsedMs = _trendTimer.restart();
    float elapsedMin = static_cast<float>(elapsedMs) / 60000.0f;

    s += QStringLiteral("\n## 趋势分析 (间隔 %1s)\n").arg(elapsedMs / 1000, 0);
    if (elapsedMs < 500) {
        s += QStringLiteral("- 采样间隔过短，趋势数据不可靠\n");
        return s;
    }

    // Battery trend
    if (_prevBatteryPct >= 0 && batPct >= 0) {
        float deltaBat = batPct - _prevBatteryPct;
        if (qAbs(deltaBat) > 0.5f) {
            float rate = deltaBat / elapsedMin;
            s += QStringLiteral("- ⚡电池: %1%→%2% (%3%/min)\n")
                .arg(_prevBatteryPct, 0, 'f', 1).arg(batPct, 0, 'f', 1)
                .arg(qAbs(rate), 0, 'f', 2);
            if (rate < -3.0f) s += QStringLiteral("  ⚠ 电量消耗过快!\n");
        }
    }

    // Altitude trend
    if (qAbs(altRel - _prevAltRel) > 0.5f) {
        float altRate = (altRel - _prevAltRel) / elapsedMin;
        s += QStringLiteral("- 📐高度: %1m→%2m (%3m/min)\n")
            .arg(_prevAltRel, 0, 'f', 1).arg(altRel, 0, 'f', 1)
            .arg(altRate, 0, 'f', 1);
    }

    // GPS quality trend
    if (_prevGpsCount >= 0 && gpsCount >= 0) {
        int deltaGps = gpsCount - _prevGpsCount;
        if (deltaGps != 0) {
            s += QStringLiteral("- 🛰 GPS卫星: %1→%2\n").arg(_prevGpsCount).arg(gpsCount);
        }
        if (_prevGpsLock != gpsLock) {
            QString lvl[] = {QStringLiteral("NoGPS"), QStringLiteral("NoFix"), QStringLiteral("2D"), QStringLiteral("3D")};
            s += QStringLiteral("- 🛰 GPS定位: %1→%2\n")
                .arg((_prevGpsLock >= 0 && _prevGpsLock < 4) ? lvl[_prevGpsLock] : QStringLiteral("?"))
                .arg((gpsLock >= 0 && gpsLock < 4) ? lvl[gpsLock] : QStringLiteral("?"));
        }
    }

    // Update cache
    _prevBatteryPct = batPct;
    _prevAltRel = altRel;
    _prevHeading = heading;
    _prevGpsCount = gpsCount;
    _prevGpsLock = gpsLock;

    return s;
}

QString ContextProvider::_collectBasicInfo(Vehicle *v) // v是一个指向Vehicle对象的指针
{
    QString s;
    s += QStringLiteral("- 固件: %1 | 类型: %2\n")
         .arg(v->firmwareTypeString(), v->vehicleTypeString());
    s += QStringLiteral("- 解锁: %1 | 飞行中: %2 | 模式: %3 | ready: %4\n")
         .arg(v->armed() ? QStringLiteral("是") : QStringLiteral("否"),
              v->flying() ? QStringLiteral("是") : QStringLiteral("否"),
              v->flightMode(),
              v->readyToFly() ? QStringLiteral("ok") : QStringLiteral("no"));
    // 向 LLM 提供飞行器实际支持的完整飞行模式列表
    // 这比 System Prompt 中的硬编码列表更准确，因为不同固件版本/机型的模式可能不同
    QStringList flightModes = v->flightModes();
    if (!flightModes.isEmpty()) {
        s += QStringLiteral("- 可用模式: %1\n").arg(flightModes.join(QStringLiteral(", ")));
    }
    return s;
}

QString ContextProvider::_collectAttitudeData(Vehicle *v)
{
    QString s;
    float roll = _safeFactValue(v->roll());
    float pitch = _safeFactValue(v->pitch());
    float heading = _safeFactValue(v->heading());
    float gs = _safeFactValue(v->groundSpeed());
    float climb = _safeFactValue(v->climbRate());
    float altRel = _safeFactValue(v->altitudeRelative());
    float distHome = _safeFactValue(v->distanceToHome());
    float throttle = _safeFactValue(v->throttlePct());

    s += QStringLiteral("- roll:%1° pitch:%2° heading:%3°\n")
         .arg(roll, 0, 'f', 1).arg(pitch, 0, 'f', 1).arg(heading, 0, 'f', 0);
    s += QStringLiteral("- 地速:%1m/s 爬升:%2m/s 高度:%3m 离家:%4m 油门:%5%\n")
         .arg(gs, 0, 'f', 1).arg(climb, 0, 'f', 1).arg(altRel, 0, 'f', 1)
         .arg(distHome, 0, 'f', 0).arg(throttle, 0, 'f', 0);
    return s;
}

QString ContextProvider::_collectBatteryData(Vehicle *v)
{
    auto *batteries = v->batteries();
    if (!batteries || batteries->count() == 0) {
        return QStringLiteral("- 无电池数据\n");
    }
    auto *batGroup = dynamic_cast<BatteryFactGroup *>(batteries->get(0));
    if (!batGroup) return QStringLiteral("- 无电池数据\n");

    auto val = [](const Fact *f) -> float {
        if (!f) return -1;
        bool ok;
        double d = f->rawValue().toDouble(&ok);
        return ok ? static_cast<float>(d) : -1;
    };

    float pct = val(batGroup->percentRemaining());
    float volt = val(batGroup->voltage());
    float curr = val(batGroup->current());
    float temp = val(batGroup->temperature());

    QString s;
    s += QStringLiteral("- 电量:%1% | 电压:%2V | 电流:%3A | 温度:%4°C\n")
         .arg(pct >= 0 ? QString::number(pct, 'f', 0) : QStringLiteral("?"),
              volt >= 0 ? QString::number(volt, 'f', 2) : QStringLiteral("?"),
              curr >= 0 ? QString::number(curr, 'f', 1) : QStringLiteral("?"),
              temp >= 0 ? QString::number(temp, 'f', 0) : QStringLiteral("?"));
    return s;
}

QString ContextProvider::_collectGpsData(Vehicle *v)
{
    auto *gps = dynamic_cast<VehicleGPSFactGroup *>(v->gpsFactGroup());
    if (!gps) return QStringLiteral("- 无GPS数据\n");

    int cnt = _safeFactInt(gps->count());
    if (cnt <= 0) return QStringLiteral("- 无GPS数据\n");

    int lock = _safeFactInt(gps->lock());
    float hdop = _safeFactValue(gps->hdop());

    QString s;
    s += QStringLiteral("- 卫星:%1 | HDOP:%2 | 定位:%3\n")
         .arg(cnt).arg(hdop, 0, 'f', 2)
         .arg(lock >= 3 ? QStringLiteral("3D Fix") : (lock >= 2 ? QStringLiteral("2D Fix") : QStringLiteral("No Lock")));
    return s;
}

QString ContextProvider::_collectHealthData(Vehicle *v) const
{
    QString s;
    s += QStringLiteral("- 传感器: %1\n")
         .arg(v->allSensorsHealthy() ? QStringLiteral("全部正常") : QStringLiteral("有异常"));
    s += QStringLiteral("- 预解锁错误: %1\n")
         .arg(v->prearmError().isEmpty() ? QStringLiteral("无") : v->prearmError());
    s += QStringLiteral("- 飞行时长: %1\n").arg(v->hobbsMeter());
    return s;
}

QString ContextProvider::_collectParameterSummary(Vehicle *v) const
{
    auto *pm = v->parameterManager();
    if (!pm || !pm->parametersReady()) return {};

    QStringList keys = {QStringLiteral("MC_ROLL_P"), QStringLiteral("MC_PITCH_P"),
                         QStringLiteral("MC_YAW_P"), QStringLiteral("BAT_N_CELLS"),
                         QStringLiteral("COM_DISARM_LAND")};

    QString s;
    s += QStringLiteral("\n## 关键参数\n");
    for (const auto &key : keys) {
        auto fact = pm->getParameter(-1, key);
        if (fact) {
            s += QStringLiteral("- %1: %2\n").arg(key).arg(fact->rawValue().toString());
        }
    }
    return s;
}

float ContextProvider::_safeFactValue(const Fact *fact, float defaultValue) const
{
    if (!fact) return defaultValue;
    QVariant v = fact->rawValue();
    if (!v.isValid() || qIsNaN(v.toDouble())) return defaultValue;
    return static_cast<float>(v.toDouble());
}

int ContextProvider::_safeFactInt(const Fact *fact, int defaultValue) const
{
    if (!fact) return defaultValue;
    QVariant v = fact->rawValue();
    if (!v.isValid()) return defaultValue;
    return v.toInt();
}
