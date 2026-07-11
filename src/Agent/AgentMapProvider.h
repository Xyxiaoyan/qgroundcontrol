#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonObject>
#include <QtPositioning/QGeoCoordinate>
#include <QtNetwork/QNetworkRequest>

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

    QString searchPOI(const QString &keywords, const QString &city, const QString &location = {}, int offset = 20);
    QString searchPOIAround(const QString &keywords, const QString &location, int radius = 3000, int offset = 20);
    QString queryWeather(const QString &city, const QString &extensions = QStringLiteral("base"));
    QString planDrivingRoute(const QString &origin, const QString &destination, const QString &strategy = {});
    QString measureDistance(const QString &origins, const QString &destination, const QString &type = QStringLiteral("0"));

private:
    // MCP protocol helpers
    void _ensureMcpInitialized();
    QString _callMcpTool(const QString &toolName, const QJsonObject &arguments);
    QJsonObject _parseMcpResponse(const QByteArray &data, const QString &contentType);
    QNetworkRequest _makeMcpRequest() const;

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
    QString _mcpEndpoint;
    QString _mcpSessionId;
    bool _mcpInitialized = false;
    int _mcpRequestId = 0;
    QString _cachedAddress;
    double _lastLat = 0;
    double _lastLon = 0;
    QElapsedTimer _lastFetchTime;
};
