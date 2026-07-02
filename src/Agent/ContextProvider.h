#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QElapsedTimer>

class Vehicle;
class AgentMapProvider;
class CameraProvider;

class ContextProvider : public QObject
{
    Q_OBJECT
public:
    explicit ContextProvider(QObject *parent = nullptr);

    QString buildVehicleContext();
    void setGeocodeApiKey(const QString &key);
    AgentMapProvider *mapProvider() const { return _map; }

private:
    QString _collectBasicInfo(Vehicle *v);
    QString _collectAttitudeData(Vehicle *v);
    QString _collectBatteryData(Vehicle *v);
    QString _collectGpsData(Vehicle *v);
    QString _collectHealthData(Vehicle *v) const;
    QString _collectParameterSummary(Vehicle *v) const;
    QString _buildTrendContext(Vehicle *v);

    float _safeFactValue(const class Fact *fact, float defaultValue = 0) const;
    int _safeFactInt(const class Fact *fact, int defaultValue = 0) const;

    AgentMapProvider *_map = nullptr;
    CameraProvider *_cam = nullptr;

    // Trend tracking
    QElapsedTimer _trendTimer;
    float _prevBatteryPct = -1;
    float _prevAltRel = 0;
    float _prevHeading = 0;
    int _prevGpsCount = -1;
    int _prevGpsLock = -1;
    int _sampleCount = 0;
};
