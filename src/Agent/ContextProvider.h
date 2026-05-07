#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

class Vehicle;
class AgentMapProvider;
class CameraProvider;

class ContextProvider : public QObject
{
    Q_OBJECT
public:
    explicit ContextProvider(QObject *parent = nullptr);

    QString buildVehicleContext();

private:
    QString _collectBasicInfo(Vehicle *v) const;
    QString _collectAttitudeData(Vehicle *v) const;
    QString _collectBatteryData(Vehicle *v) const;
    QString _collectGpsData(Vehicle *v) const;
    QString _collectHealthData(Vehicle *v) const;
    QString _collectParameterSummary(Vehicle *v) const;

    float _safeFactValue(const class Fact *fact, float defaultValue = 0) const;
    int _safeFactInt(const class Fact *fact, int defaultValue = 0) const;

    AgentMapProvider *_map = nullptr;
    CameraProvider *_cam = nullptr;
};
