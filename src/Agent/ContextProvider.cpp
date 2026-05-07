#include "ContextProvider.h"
#include "AgentMapProvider.h"
#include "CameraProvider.h"
#include "Vehicle.h"
#include "Fact.h"
#include "ParameterManager.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"

QGC_LOGGING_CATEGORY(ContextLog, "qgc.agent.ctx")

ContextProvider::ContextProvider(QObject *parent)
    : QObject(parent)
    , _map(new AgentMapProvider(this))
    , _cam(new CameraProvider(this))
{
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

    return ctx;
}

QString ContextProvider::_collectBasicInfo(Vehicle *v) const
{
    QString s;
    s += QStringLiteral("- 固件: %1 | 类型: %2\n")
         .arg(v->firmwareTypeString(), v->vehicleTypeString());
    s += QStringLiteral("- 解锁: %1 | 飞行中: %2 | 模式: %3 | ready: %4\n")
         .arg(v->armed() ? QStringLiteral("是") : QStringLiteral("否"),
              v->flying() ? QStringLiteral("是") : QStringLiteral("否"),
              v->flightMode(),
              v->readyToFly() ? QStringLiteral("ok") : QStringLiteral("no"));
    return s;
}

QString ContextProvider::_collectAttitudeData(Vehicle *v) const
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

QString ContextProvider::_collectBatteryData(Vehicle *v) const
{
    auto *batteries = v->batteries();
    if (!batteries || batteries->count() == 0) {
        return QStringLiteral("- 无电池数据\n");
    }
    auto *bat = batteries->get(0);
    if (!bat) return QStringLiteral("- 无电池数据\n");

    auto val = [bat](const char *name) -> float {
        QVariant v = bat->property(name);
        if (!v.isValid()) return -1;
        auto *fact = v.value<Fact *>();
        if (!fact) return -1;
        bool ok;
        double d = fact->rawValue().toDouble(&ok);
        return ok ? static_cast<float>(d) : -1;
    };

    float pct = val("percentRemaining");
    float volt = val("voltage");
    float curr = val("current");
    float temp = val("temperature");

    QString s;
    s += QStringLiteral("- 电量:%1% | 电压:%2V | 电流:%3A | 温度:%4°C\n")
         .arg(pct >= 0 ? QString::number(pct, 'f', 0) : QStringLiteral("?"),
              volt >= 0 ? QString::number(volt, 'f', 2) : QStringLiteral("?"),
              curr >= 0 ? QString::number(curr, 'f', 1) : QStringLiteral("?"),
              temp >= 0 ? QString::number(temp, 'f', 0) : QStringLiteral("?"));
    return s;
}

QString ContextProvider::_collectGpsData(Vehicle *v) const
{
    auto *gps = v->gpsFactGroup();
    if (!gps) return QStringLiteral("- 无GPS数据\n");

    auto *countFact = gps->findChild<Fact *>(QStringLiteral("count"), Qt::FindDirectChildrenOnly);
    int cnt = countFact ? _safeFactInt(countFact) : 0;
    if (cnt <= 0) return QStringLiteral("- 无GPS数据\n");

    auto *lockFact = gps->findChild<Fact *>(QStringLiteral("lock"), Qt::FindDirectChildrenOnly);
    auto *hdopFact = gps->findChild<Fact *>(QStringLiteral("hdop"), Qt::FindDirectChildrenOnly);

    int lock = lockFact ? _safeFactInt(lockFact) : 0;
    float hdop = hdopFact ? _safeFactValue(hdopFact) : -1.0f;

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
