#include "AgentMapProvider.h"
#include "Vehicle.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(MapLog, "qgc.agent.map")

AgentMapProvider::AgentMapProvider(QObject *parent) : QObject(parent) {}

QString AgentMapProvider::buildMapContext(Vehicle *v) const
{
    if (!v) return {};
    QString ctx;
    ctx += QStringLiteral("\n## 地图与定位\n");
    ctx += _collectPositionData(v);
    ctx += _collectMissionInfo(v);
    return ctx;
}

QString AgentMapProvider::_collectPositionData(Vehicle *v) const
{
    QGeoCoordinate coord = v->coordinate();
    QGeoCoordinate home = v->homePosition();

    QString s;
    s += QStringLiteral("- 当前位置: lat=%1 lon=%2 alt=%3m\n")
         .arg(coord.latitude(), 0, 'f', 6)
         .arg(coord.longitude(), 0, 'f', 6)
         .arg(coord.altitude(), 0, 'f', 1);
    s += QStringLiteral("- 返航点: lat=%1 lon=%2 alt=%3m\n")
         .arg(home.latitude(), 0, 'f', 6)
         .arg(home.longitude(), 0, 'f', 6)
         .arg(home.altitude(), 0, 'f', 1);

    if (coord.isValid() && home.isValid()) {
        double dist = coord.distanceTo(home);
        double bearing = coord.azimuthTo(home);
        s += QStringLiteral("- 离家距离: %1m 方位: %2°\n").arg(dist, 0, 'f', 0).arg(bearing, 0, 'f', 0);
    }
    return s;
}

QString AgentMapProvider::_collectMissionInfo(Vehicle *v) const
{
    int missionIdx = -1;
    auto *miFact = v->missionItemIndex();
    if (miFact) {
        missionIdx = miFact->rawValue().toInt();
    }

    QString s;
    if (missionIdx >= 0) {
        s += QStringLiteral("- 当前航点: #%1\n").arg(missionIdx);
    } else {
        s += QStringLiteral("- 当前无航线\n");
    }
    return s;
}
