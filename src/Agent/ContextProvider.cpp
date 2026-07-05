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
        return QStringLiteral("No vehicle connected.");
    }

    QString ctx;
    ctx += QStringLiteral("## Vehicle Status\n");
    ctx += _collectBasicInfo(v);
    ctx += QStringLiteral("\n## Attitude & Motion\n");
    ctx += _collectAttitudeData(v);
    ctx += QStringLiteral("\n## Battery\n");
    ctx += _collectBatteryData(v);
    ctx += QStringLiteral("\n## GPS\n");
    ctx += _collectGpsData(v);
    ctx += QStringLiteral("\n## Health & Comms\n");
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

    s += QStringLiteral("\n## Trend Analysis (interval %1s)\n").arg(elapsedMs / 1000, 0);
    if (elapsedMs < 500) {
        s += QStringLiteral("- Sampling interval too short, trend data unreliable\n");
        return s;
    }

    // Battery trend
    if (_prevBatteryPct >= 0 && batPct >= 0) {
        float deltaBat = batPct - _prevBatteryPct;
        if (qAbs(deltaBat) > 0.5f) {
            float rate = deltaBat / elapsedMin;
            s += QStringLiteral("- Battery: %1%->%2% (%3%/min)\n")
                .arg(_prevBatteryPct, 0, 'f', 1).arg(batPct, 0, 'f', 1)
                .arg(qAbs(rate), 0, 'f', 2);
            if (rate < -3.0f) s += QStringLiteral("  WARNING: Battery draining too fast!\n");
        }
    }

    // Altitude trend
    if (qAbs(altRel - _prevAltRel) > 0.5f) {
        float altRate = (altRel - _prevAltRel) / elapsedMin;
        s += QStringLiteral("- Altitude: %1m->%2m (%3m/min)\n")
            .arg(_prevAltRel, 0, 'f', 1).arg(altRel, 0, 'f', 1)
            .arg(altRate, 0, 'f', 1);
    }

    // GPS quality trend
    if (_prevGpsCount >= 0 && gpsCount >= 0) {
        int deltaGps = gpsCount - _prevGpsCount;
        if (deltaGps != 0) {
            s += QStringLiteral("- GPS sats: %1->%2\n").arg(_prevGpsCount).arg(gpsCount);
        }
        if (_prevGpsLock != gpsLock) {
            QString lvl[] = {QStringLiteral("NoGPS"), QStringLiteral("NoFix"), QStringLiteral("2D"), QStringLiteral("3D")};
            s += QStringLiteral("- GPS lock: %1->%2\n")
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

QString ContextProvider::_collectBasicInfo(Vehicle *v)
{
    QString s;
    s += QStringLiteral("- Firmware: %1 | Type: %2\n")
         .arg(v->firmwareTypeString(), v->vehicleTypeString());
    s += QStringLiteral("- Armed: %1 | Flying: %2 | Mode: %3 | Ready: %4\n")
         .arg(v->armed() ? QStringLiteral("Yes") : QStringLiteral("No"),
              v->flying() ? QStringLiteral("Yes") : QStringLiteral("No"),
              v->flightMode(),
              v->readyToFly() ? QStringLiteral("ok") : QStringLiteral("no"));
    // Provide the full list of flight modes supported by the vehicle
    QStringList flightModes = v->flightModes();
    if (!flightModes.isEmpty()) {
        s += QStringLiteral("- Available modes: %1\n").arg(flightModes.join(QStringLiteral(", ")));
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
    s += QStringLiteral("- Ground speed:%1m/s Climb:%2m/s Alt:%3m Dist home:%4m Throttle:%5%\n")
         .arg(gs, 0, 'f', 1).arg(climb, 0, 'f', 1).arg(altRel, 0, 'f', 1)
         .arg(distHome, 0, 'f', 0).arg(throttle, 0, 'f', 0);
    return s;
}

QString ContextProvider::_collectBatteryData(Vehicle *v)
{
    auto *batteries = v->batteries();
    if (!batteries || batteries->count() == 0) {
        return QStringLiteral("- No battery data\n");
    }
    auto *batGroup = dynamic_cast<BatteryFactGroup *>(batteries->get(0));
    if (!batGroup) return QStringLiteral("- No battery data\n");

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
    s += QStringLiteral("- Charge:%1% | Voltage:%2V | Current:%3A | Temp:%4C\n")
         .arg(pct >= 0 ? QString::number(pct, 'f', 0) : QStringLiteral("?"),
              volt >= 0 ? QString::number(volt, 'f', 2) : QStringLiteral("?"),
              curr >= 0 ? QString::number(curr, 'f', 1) : QStringLiteral("?"),
              temp >= 0 ? QString::number(temp, 'f', 0) : QStringLiteral("?"));
    return s;
}

QString ContextProvider::_collectGpsData(Vehicle *v)
{
    auto *gps = dynamic_cast<VehicleGPSFactGroup *>(v->gpsFactGroup());
    if (!gps) return QStringLiteral("- No GPS data\n");

    int cnt = _safeFactInt(gps->count());
    if (cnt <= 0) return QStringLiteral("- No GPS data\n");

    int lock = _safeFactInt(gps->lock());
    float hdop = _safeFactValue(gps->hdop());

    QString s;
    s += QStringLiteral("- Sats:%1 | HDOP:%2 | Lock:%3\n")
         .arg(cnt).arg(hdop, 0, 'f', 2)
         .arg(lock >= 3 ? QStringLiteral("3D Fix") : (lock >= 2 ? QStringLiteral("2D Fix") : QStringLiteral("No Lock")));
    return s;
}

QString ContextProvider::_collectHealthData(Vehicle *v) const
{
    QString s;
    s += QStringLiteral("- Sensors: %1\n")
         .arg(v->allSensorsHealthy() ? QStringLiteral("All normal") : QStringLiteral("Issues detected"));
    s += QStringLiteral("- Prearm error: %1\n")
         .arg(v->prearmError().isEmpty() ? QStringLiteral("None") : v->prearmError());
    s += QStringLiteral("- Flight time: %1\n").arg(v->hobbsMeter());
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
    s += QStringLiteral("\n## Key Parameters\n");
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
