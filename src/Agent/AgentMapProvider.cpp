#include "AgentMapProvider.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtCore/QEventLoop>

QGC_LOGGING_CATEGORY(MapLog, "qgc.agent.map")

AgentMapProvider::AgentMapProvider(QObject *parent)
    : QObject(parent)
    , _network(new QNetworkAccessManager(this))
    , _refreshTimer(new QTimer(this))
{
    _refreshTimer->setInterval(30000);
    connect(_refreshTimer, &QTimer::timeout, this, &AgentMapProvider::_onRefreshTimer);
}

void AgentMapProvider::setGeocodeApiKey(const QString &key)
{
    if (_geocodeApiKey == key) {
        qWarning() << "[MCP] Key unchanged, skipping";
        return;
    }
    _geocodeApiKey = key;
    _mcpInitialized = false;
    _mcpSessionId.clear();

    if (key.isEmpty()) {
        _mcpEndpoint.clear();
        _refreshTimer->stop();
        qWarning() << "[MCP] Key cleared";
        return;
    }

    _mcpEndpoint = QStringLiteral("https://mcp.amap.com/mcp?key=%1").arg(key);
    _refreshTimer->start();
    qWarning() << "[MCP] Key set, endpoint:" << _mcpEndpoint;

    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (v) {
        QGeoCoordinate coord = v->coordinate();
        if (coord.isValid()) {
            _triggerGeocode(coord.latitude(), coord.longitude());
        } else {
            qWarning() << "[MCP] Vehicle connected but coord invalid, waiting for timer";
        }
    } else {
        qWarning() << "[MCP] No vehicle, listening for activeVehicleChanged";
        connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged,
                this, [this](Vehicle *vehicle) {
            if (!vehicle || _geocodeApiKey.isEmpty()) return;
            QGeoCoordinate coord = vehicle->coordinate();
            if (coord.isValid()) {
                qWarning() << "[MCP] Vehicle connected, triggering first query";
                _triggerGeocode(coord.latitude(), coord.longitude());
            }
        }, Qt::SingleShotConnection);
    }
}

void AgentMapProvider::requestAddress()
{
    if (_geocodeApiKey.isEmpty()) {
        qWarning() << "[MCP] requestAddress but Key is empty";
        return;
    }
    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        qWarning() << "[MCP] requestAddress but no vehicle";
        return;
    }
    QGeoCoordinate coord = v->coordinate();
    if (!coord.isValid()) {
        qWarning() << "[MCP] requestAddress but coord invalid";
        return;
    }
    Q_UNUSED(v);
    _triggerGeocode(coord.latitude(), coord.longitude());
}

// ═══ MCP protocol helpers ═══

QNetworkRequest AgentMapProvider::_makeMcpRequest() const
{
    QNetworkRequest req((QUrl(_mcpEndpoint)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json, text/event-stream");
    if (!_mcpSessionId.isEmpty()) {
        req.setRawHeader("Mcp-Session-Id", _mcpSessionId.toUtf8());
    }
    req.setTransferTimeout(10000);
    return req;
}

void AgentMapProvider::_ensureMcpInitialized()
{
    if (_mcpInitialized || _mcpEndpoint.isEmpty()) return;

    QJsonObject request;
    request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    request[QStringLiteral("id")] = ++_mcpRequestId;
    request[QStringLiteral("method")] = QStringLiteral("initialize");

    QJsonObject params;
    params[QStringLiteral("protocolVersion")] = QStringLiteral("2024-11-05");
    params[QStringLiteral("capabilities")] = QJsonObject{};

    QJsonObject clientInfo;
    clientInfo[QStringLiteral("name")] = QStringLiteral("qgc-agent");
    clientInfo[QStringLiteral("version")] = QStringLiteral("1.0.0");
    params[QStringLiteral("clientInfo")] = clientInfo;
    request[QStringLiteral("params")] = params;

    QNetworkRequest req = _makeMcpRequest();
    QNetworkReply *reply = _network->post(req, QJsonDocument(request).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->hasRawHeader("Mcp-Session-Id")) {
        _mcpSessionId = QString::fromUtf8(reply->rawHeader("Mcp-Session-Id"));
        qWarning() << "[MCP] Session ID:" << _mcpSessionId;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[MCP] Initialize failed:" << reply->errorString();
        reply->deleteLater();
        return;
    }

    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonObject response = _parseMcpResponse(data, contentType);
    if (response.isEmpty()) {
        qWarning() << "[MCP] Initialize response parse failed";
        return;
    }

    if (response.contains(QStringLiteral("error"))) {
        qWarning() << "[MCP] Initialize error:" << response[QStringLiteral("error")].toObject()[QStringLiteral("message")].toString();
        return;
    }

    qWarning() << "[MCP] Initialized successfully";
    _mcpInitialized = true;

    // Send notifications/initialized (fire-and-forget)
    QJsonObject notif;
    notif[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    notif[QStringLiteral("method")] = QStringLiteral("notifications/initialized");

    QNetworkRequest notifReq = _makeMcpRequest();
    QNetworkReply *notifReply = _network->post(notifReq, QJsonDocument(notif).toJson(QJsonDocument::Compact));
    notifReply->deleteLater();
}

QString AgentMapProvider::_callMcpTool(const QString &toolName, const QJsonObject &arguments)
{
    _ensureMcpInitialized();
    if (!_mcpInitialized) {
        qWarning() << "[MCP] Not initialized, cannot call tool:" << toolName;
        return {};
    }

    QJsonObject request;
    request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    request[QStringLiteral("id")] = ++_mcpRequestId;
    request[QStringLiteral("method")] = QStringLiteral("tools/call");

    QJsonObject params;
    params[QStringLiteral("name")] = toolName;
    params[QStringLiteral("arguments")] = arguments;
    request[QStringLiteral("params")] = params;

    QNetworkRequest req = _makeMcpRequest();
    QNetworkReply *reply = _network->post(req, QJsonDocument(request).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->hasRawHeader("Mcp-Session-Id")) {
        _mcpSessionId = QString::fromUtf8(reply->rawHeader("Mcp-Session-Id"));
    }

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[MCP] Network error:" << reply->errorString();
        reply->deleteLater();
        return {};
    }

    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonObject response = _parseMcpResponse(data, contentType);
    if (response.isEmpty()) {
        qWarning() << "[MCP] Failed to parse response for tool:" << toolName;
        return {};
    }

    if (response.contains(QStringLiteral("error"))) {
        QJsonObject err = response[QStringLiteral("error")].toObject();
        qWarning() << "[MCP] RPC error:" << err[QStringLiteral("message")].toString();
        return {};
    }

    QJsonObject result = response[QStringLiteral("result")].toObject();
    if (result[QStringLiteral("isError")].toBool()) {
        QJsonArray content = result[QStringLiteral("content")].toArray();
        if (!content.isEmpty()) {
            qWarning() << "[MCP] Tool error:" << content.first().toObject()[QStringLiteral("text")].toString();
        }
        return {};
    }

    QJsonArray content = result[QStringLiteral("content")].toArray();
    if (content.isEmpty()) {
        qWarning() << "[MCP] Empty content for tool:" << toolName;
        return {};
    }

    return content.first().toObject()[QStringLiteral("text")].toString();
}

QJsonObject AgentMapProvider::_parseMcpResponse(const QByteArray &data, const QString &contentType)
{
    QJsonDocument doc;

    if (contentType.contains(QStringLiteral("text/event-stream"), Qt::CaseInsensitive)) {
        // SSE format: parse "data:" lines
        QString text = QString::fromUtf8(data);
        for (const QString &line : text.split('\n')) {
            QString trimmed = line.trimmed();
            if (trimmed.startsWith(QStringLiteral("data:"))) {
                QString jsonStr = trimmed.mid(5).trimmed();
                doc = QJsonDocument::fromJson(jsonStr.toUtf8());
                if (!doc.isNull()) break;
            }
        }
    } else {
        doc = QJsonDocument::fromJson(data);
    }

    if (doc.isNull() || !doc.isObject()) {
        return {};
    }
    return doc.object();
}

// ═══ Public API methods (now using MCP tools) ═══

QString AgentMapProvider::resolveAddressSync()
{
    if (_geocodeApiKey.isEmpty()) {
        qWarning() << "[MCP] resolveAddressSync: Key is empty";
        return {};
    }

    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        qWarning() << "[MCP] resolveAddressSync: no vehicle";
        return {};
    }

    QGeoCoordinate coord = v->coordinate();
    if (!coord.isValid()) {
        qWarning() << "[MCP] resolveAddressSync: coord invalid";
        return {};
    }

    double lat = coord.latitude();
    double lon = coord.longitude();

    QGeoCoordinate cur(lat, lon);
    QGeoCoordinate prev(_lastLat, _lastLon);
    if (!_cachedAddress.isEmpty() && prev.isValid() && cur.isValid()) {
        if (cur.distanceTo(prev) < 50.0) {
            return _cachedAddress;
        }
    }

    QJsonObject args;
    args[QStringLiteral("location")] = QStringLiteral("%1,%2").arg(lon, 0, 'f', 6).arg(lat, 0, 'f', 6);

    QString text = _callMcpTool(QStringLiteral("maps_regeocode"), args);
    if (text.isEmpty()) return {};

    QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (doc.isNull()) return {};

    QJsonObject obj = doc.object();
    // Handle both raw REST API format and simplified MCP format
    QString address = obj[QStringLiteral("formatted_address")].toString();
    if (address.isEmpty()) {
        address = obj[QStringLiteral("regeocode")]
                      .toObject()[QStringLiteral("formatted_address")]
                      .toString();
    }

    if (!address.isEmpty()) {
        _cachedAddress = address;
        _lastFetchTime.start();
        _lastLat = lat;
        _lastLon = lon;
        emit cachedAddressChanged();
    }
    return address;
}

QString AgentMapProvider::resolveForwardGeocode(const QString &address, const QString &city)
{
    if (_geocodeApiKey.isEmpty()) {
        qWarning() << "[MCP] forward: Key is empty";
        return {};
    }
    if (address.trimmed().isEmpty()) {
        qWarning() << "[MCP] forward: address is empty";
        return {};
    }

    QJsonObject args;
    args[QStringLiteral("address")] = address.trimmed();
    if (!city.trimmed().isEmpty()) {
        args[QStringLiteral("city")] = city.trimmed();
    }

    QString text = _callMcpTool(QStringLiteral("maps_geo"), args);
    if (text.isEmpty()) return {};

    QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (doc.isNull()) return {};

    QJsonObject obj = doc.object();

    // Try raw REST API format: {geocodes: [{location, formatted_address}]}
    QJsonArray geocodes = obj[QStringLiteral("geocodes")].toArray();
    if (!geocodes.isEmpty()) {
        QJsonObject first = geocodes.first().toObject();
        QString location = first[QStringLiteral("location")].toString();
        QString formattedAddress = first[QStringLiteral("formatted_address")].toString();

        if (!location.isEmpty()) {
            QStringList parts = location.split(',');
            QJsonObject result;
            result[QStringLiteral("status")] = QStringLiteral("success");
            result[QStringLiteral("address")] = formattedAddress;
            result[QStringLiteral("location")] = location;
            if (parts.size() == 2) {
                result[QStringLiteral("lng")] = parts[0].trimmed().toDouble();
                result[QStringLiteral("lat")] = parts[1].trimmed().toDouble();
            }
            return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
        }
    }

    // Try simplified MCP format: {location, formatted_address}
    QString location = obj[QStringLiteral("location")].toString();
    if (!location.isEmpty()) {
        QJsonObject result;
        result[QStringLiteral("status")] = QStringLiteral("success");
        result[QStringLiteral("address")] = obj[QStringLiteral("formatted_address")].toString();
        result[QStringLiteral("location")] = location;
        QStringList parts = location.split(',');
        if (parts.size() == 2) {
            result[QStringLiteral("lng")] = parts[0].trimmed().toDouble();
            result[QStringLiteral("lat")] = parts[1].trimmed().toDouble();
        }
        return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }

    qWarning() << "[MCP] forward: no results";
    return {};
}

QString AgentMapProvider::searchPOI(const QString &keywords, const QString &city, const QString &location, int offset)
{
    if (_geocodeApiKey.isEmpty()) {
        qWarning() << "[MCP] searchPOI: Key is empty";
        return {};
    }
    if (keywords.trimmed().isEmpty()) {
        qWarning() << "[MCP] searchPOI: keywords is empty";
        return {};
    }
    Q_UNUSED(location)
    Q_UNUSED(offset)

    QJsonObject args;
    args[QStringLiteral("keywords")] = keywords.trimmed();
    if (!city.trimmed().isEmpty()) {
        args[QStringLiteral("city")] = city.trimmed();
    }

    return _callMcpTool(QStringLiteral("maps_search_text"), args);
}

QString AgentMapProvider::searchPOIAround(const QString &keywords, const QString &location, int radius, int offset)
{
    if (_geocodeApiKey.isEmpty()) {
        qWarning() << "[MCP] searchPOIAround: Key is empty";
        return {};
    }
    if (keywords.trimmed().isEmpty()) {
        qWarning() << "[MCP] searchPOIAround: keywords is empty";
        return {};
    }
    if (location.trimmed().isEmpty()) {
        qWarning() << "[MCP] searchPOIAround: location is empty";
        return {};
    }
    Q_UNUSED(offset)

    QJsonObject args;
    args[QStringLiteral("keywords")] = keywords.trimmed();
    args[QStringLiteral("location")] = location.trimmed();
    args[QStringLiteral("radius")] = radius;

    return _callMcpTool(QStringLiteral("maps_search_around"), args);
}

QString AgentMapProvider::queryWeather(const QString &city, const QString &extensions)
{
    if (_geocodeApiKey.isEmpty()) {
        qWarning() << "[MCP] queryWeather: Key is empty";
        return {};
    }
    if (city.trimmed().isEmpty()) {
        qWarning() << "[MCP] queryWeather: city is empty";
        return {};
    }
    Q_UNUSED(extensions)

    QJsonObject args;
    args[QStringLiteral("city")] = city.trimmed();

    return _callMcpTool(QStringLiteral("maps_weather"), args);
}

QString AgentMapProvider::planDrivingRoute(const QString &origin, const QString &destination, const QString &strategy)
{
    if (_geocodeApiKey.isEmpty()) {
        qWarning() << "[MCP] planDrivingRoute: Key is empty";
        return {};
    }
    if (origin.trimmed().isEmpty() || destination.trimmed().isEmpty()) {
        qWarning() << "[MCP] planDrivingRoute: origin or destination is empty";
        return {};
    }
    Q_UNUSED(strategy)

    QJsonObject args;
    args[QStringLiteral("origin")] = origin.trimmed();
    args[QStringLiteral("destination")] = destination.trimmed();

    return _callMcpTool(QStringLiteral("maps_direction_driving"), args);
}

QString AgentMapProvider::measureDistance(const QString &origins, const QString &destination, const QString &type)
{
    if (_geocodeApiKey.isEmpty()) {
        qWarning() << "[MCP] measureDistance: Key is empty";
        return {};
    }
    if (origins.trimmed().isEmpty() || destination.trimmed().isEmpty()) {
        qWarning() << "[MCP] measureDistance: origins or destination is empty";
        return {};
    }

    QJsonObject args;
    args[QStringLiteral("origins")] = origins.trimmed();
    args[QStringLiteral("destination")] = destination.trimmed();
    args[QStringLiteral("type")] = type;

    return _callMcpTool(QStringLiteral("maps_distance"), args);
}

// ═══ Background geocode refresh ═══

void AgentMapProvider::_onRefreshTimer()
{
    if (_geocodeApiKey.isEmpty()) {
        return;
    }

    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        return;
    }

    QGeoCoordinate coord = v->coordinate();
    if (!coord.isValid()) {
        return;
    }

    double lat = coord.latitude();
    double lon = coord.longitude();

    if (_lastLat == 0 && _lastLon == 0) {
        _triggerGeocode(lat, lon);
    } else {
        QGeoCoordinate cur(lat, lon);
        QGeoCoordinate prev(_lastLat, _lastLon);
        if (cur.distanceTo(prev) > 50.0) {
            _triggerGeocode(lat, lon);
        }
    }
}

void AgentMapProvider::_triggerGeocode(double lat, double lon)
{
    if (_geocodeApiKey.isEmpty()) {
        qWarning() << "[MCP] Key is empty, cannot trigger query";
        return;
    }

    // Ensure MCP session is initialized (synchronous, one-time)
    _ensureMcpInitialized();
    if (!_mcpInitialized) {
        qWarning() << "[MCP] Not initialized, skipping geocode";
        return;
    }

    _lastLat = lat;
    _lastLon = lon;

    QJsonObject request;
    request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    request[QStringLiteral("id")] = ++_mcpRequestId;
    request[QStringLiteral("method")] = QStringLiteral("tools/call");

    QJsonObject params;
    params[QStringLiteral("name")] = QStringLiteral("maps_regeocode");
    QJsonObject args;
    args[QStringLiteral("location")] = QStringLiteral("%1,%2").arg(lon, 0, 'f', 6).arg(lat, 0, 'f', 6);
    params[QStringLiteral("arguments")] = args;
    request[QStringLiteral("params")] = params;

    QNetworkRequest req = _makeMcpRequest();
    QNetworkReply *reply = _network->post(req, QJsonDocument(request).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[MCP] Geocode request failed:" << reply->errorString();
            return;
        }

        QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        QByteArray data = reply->readAll();

        QJsonObject response = _parseMcpResponse(data, contentType);
        if (response.isEmpty()) {
            qWarning() << "[MCP] Geocode response parse failed";
            return;
        }

        if (response.contains(QStringLiteral("error"))) {
            qWarning() << "[MCP] Geocode RPC error:" << response[QStringLiteral("error")].toObject()[QStringLiteral("message")].toString();
            return;
        }

        QJsonObject result = response[QStringLiteral("result")].toObject();
        if (result[QStringLiteral("isError")].toBool()) {
            qWarning() << "[MCP] Geocode tool error";
            return;
        }

        QJsonArray content = result[QStringLiteral("content")].toArray();
        if (content.isEmpty()) return;

        QString text = content.first().toObject()[QStringLiteral("text")].toString();
        QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (doc.isNull()) return;

        QJsonObject obj = doc.object();
        // Handle both raw REST API format and simplified MCP format
        QString address = obj[QStringLiteral("formatted_address")].toString();
        if (address.isEmpty()) {
            address = obj[QStringLiteral("regeocode")]
                          .toObject()[QStringLiteral("formatted_address")]
                          .toString();
        }

        if (!address.isEmpty() && _cachedAddress != address) {
            _cachedAddress = address;
            _lastFetchTime.start();
            qWarning() << "[MCP] Address updated:" << _cachedAddress;
            emit cachedAddressChanged();
        }
    });
}

// ═══ Vehicle context (unchanged) ═══

QString AgentMapProvider::buildMapContext(Vehicle *v) const
{
    if (!v) return {};
    QString ctx;
    ctx += QStringLiteral("\n## Map & Positioning\n");
    ctx += _collectPositionData(v);
    ctx += _collectMissionInfo(v);
    return ctx;
}

QString AgentMapProvider::_collectPositionData(Vehicle *v) const
{
    QGeoCoordinate coord = v->coordinate();
    QGeoCoordinate home = v->homePosition();

    QString s;
    s += QStringLiteral("- Position: lat=%1 lon=%2 alt=%3m\n")
         .arg(coord.latitude(), 0, 'f', 6)
         .arg(coord.longitude(), 0, 'f', 6)
         .arg(coord.altitude(), 0, 'f', 1);

    s += QStringLiteral("- Home: lat=%1 lon=%2 alt=%3m\n")
         .arg(home.latitude(), 0, 'f', 6)
         .arg(home.longitude(), 0, 'f', 6)
         .arg(home.altitude(), 0, 'f', 1);

    if (coord.isValid() && home.isValid()) {
        double dist = coord.distanceTo(home);
        double bearing = coord.azimuthTo(home);
        s += QStringLiteral("- Dist to home: %1m Bearing: %2deg\n").arg(dist, 0, 'f', 0).arg(bearing, 0, 'f', 0);
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
        s += QStringLiteral("- Current waypoint: #%1\n").arg(missionIdx);
    } else {
        s += QStringLiteral("- No active mission\n");
    }
    return s;
}
