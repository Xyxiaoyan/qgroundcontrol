#pragma once

#include "../AgentTool.h"

#include <QtCore/QList>
#include <QtCore/QJsonObject>

class MissionTool : public AgentTool
{
    Q_OBJECT
public:
    explicit MissionTool(QObject *parent = nullptr);
    ~MissionTool() override;

    QString name() const override { return QStringLiteral("mission"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override { return false; }

private:
    struct Waypoint {
        double lat;
        double lon;
        double alt;
        double yaw;           // NaN = vehicle default
        double acceptance;    // 0 = vehicle default
        double holdTime;      // seconds
    };
    QList<Waypoint> _waypoints;
    bool _hasTakeoff = false;
    double _takeoffAlt = 50.0;

    QJsonObject _waypointToJson(int index, const Waypoint &wp) const;
};
