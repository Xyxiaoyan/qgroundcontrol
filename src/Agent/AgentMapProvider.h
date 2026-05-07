#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtPositioning/QGeoCoordinate>

class Vehicle;

class AgentMapProvider : public QObject
{
    Q_OBJECT
public:
    explicit AgentMapProvider(QObject *parent = nullptr);

    QString buildMapContext(Vehicle *v) const;

private:
    QString _collectPositionData(Vehicle *v) const;
    QString _collectMissionInfo(Vehicle *v) const;
};
