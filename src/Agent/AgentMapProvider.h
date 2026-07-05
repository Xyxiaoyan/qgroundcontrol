#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include <QtPositioning/QGeoCoordinate>

class Vehicle;
class QNetworkAccessManager;
class QNetworkReply;

class AgentMapProvider : public QObject
{
    Q_OBJECT
public:
    explicit AgentMapProvider(QObject *parent = nullptr);

    QString buildMapContext(Vehicle *v) const;

    QString geocodeApiKey() const { return _geocodeApiKey; }
    void setGeocodeApiKey(const QString &key);

    QString cachedAddress() const { return _cachedAddress; }
    void requestAddress();
    QString resolveAddressSync();
    QString resolveForwardGeocode(const QString &address, const QString &city = {});

signals:
    void cachedAddressChanged();

private slots:
    void _onRefreshTimer();

private:
    void _triggerGeocode(double lat, double lon);
    QString _collectPositionData(Vehicle *v) const;
    QString _collectMissionInfo(Vehicle *v) const;

    QNetworkAccessManager *_network = nullptr;
    QTimer *_refreshTimer = nullptr;
    QString _geocodeApiKey;
    QString _cachedAddress;
    double _lastLat = 0;
    double _lastLon = 0;
    QElapsedTimer _lastFetchTime;
};
