#include "AgentMapProvider.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
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
    _refreshTimer->setInterval(30000); // 每 30 秒刷新一次
    connect(_refreshTimer, &QTimer::timeout, this, &AgentMapProvider::_onRefreshTimer);
}

void AgentMapProvider::setGeocodeApiKey(const QString &key)
{
    if (_geocodeApiKey == key) {
        qWarning() << "[Geocode] Key 未变化，跳过";
        return;
    }
    _geocodeApiKey = key;
    _refreshTimer->start();
    qWarning() << "[Geocode] Key 已设置, timer 已启动";

    // 如果已有飞行器，立即触发一次查询
    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (v) {
        QGeoCoordinate coord = v->coordinate();
        if (coord.isValid()) {
            _triggerGeocode(coord.latitude(), coord.longitude());
        } else {
            qWarning() << "[Geocode] 飞机已连接但坐标无效, 等待 timer";
        }
    } else {
        qWarning() << "[Geocode] 无飞机连接, 监听 activeVehicleChanged";
        // 等飞机连上后立刻触发
        connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged,
                this, [this](Vehicle *vehicle) {
            if (!vehicle || _geocodeApiKey.isEmpty()) return;
            QGeoCoordinate coord = vehicle->coordinate();
            if (coord.isValid()) {
                qWarning() << "[Geocode] 飞机连接, 触发首次查询";
                _triggerGeocode(coord.latitude(), coord.longitude());
            }
        }, Qt::SingleShotConnection); // 只触发一次，之后靠 timer
    }
}

void AgentMapProvider::requestAddress()
{
    if (_geocodeApiKey.isEmpty()) {
        qWarning() << "[Geocode] requestAddress 但 Key 为空";
        return;
    }
    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        qWarning() << "[Geocode] requestAddress 但无飞机连接";
        return;
    }
    QGeoCoordinate coord = v->coordinate();
    if (!coord.isValid()) {
        qWarning() << "[Geocode] requestAddress 但坐标无效";
        return;
    }
    Q_UNUSED(v);
    // 无视 50m 限制，直接触发
    _triggerGeocode(coord.latitude(), coord.longitude());
}

QString AgentMapProvider::resolveAddressSync()
{
    if (_geocodeApiKey.isEmpty()) {
        qWarning() << "[Geocode] resolveAddressSync: Key 为空";
        return {};
    }

    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        qWarning() << "[Geocode] resolveAddressSync: 无飞机";
        return {};
    }

    QGeoCoordinate coord = v->coordinate();
    if (!coord.isValid()) {
        qWarning() << "[Geocode] resolveAddressSync: 坐标无效";
        return {};
    }

    double lat = coord.latitude();
    double lon = coord.longitude();

    // Reuse cache if position unchanged (< 50m)
    QGeoCoordinate cur(lat, lon);
    QGeoCoordinate prev(_lastLat, _lastLon);
    if (!_cachedAddress.isEmpty() && prev.isValid() && cur.isValid()) {
        if (cur.distanceTo(prev) < 50.0) {
            return _cachedAddress;
        }
    }

    QUrl url(QStringLiteral("https://restapi.amap.com/v3/geocode/regeo"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), _geocodeApiKey);
    query.addQueryItem(QStringLiteral("location"), QStringLiteral("%1,%2").arg(lon, 0, 'f', 6).arg(lat, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("radius"), QStringLiteral("500"));
    query.addQueryItem(QStringLiteral("extensions"), QStringLiteral("base"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(10000);

    QNetworkReply *reply = _network->get(request);
    if (!reply) {
        qWarning() << "[Geocode] resolveAddressSync: get() 返回 nullptr";
        return {};
    }

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[Geocode] resolveAddressSync: 网络错误" << reply->errorString();
        return {};
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "[Geocode] resolveAddressSync: JSON 解析失败" << err.errorString();
        return {};
    }

    QJsonObject obj = doc.object();
    if (obj[QStringLiteral("status")].toString() != QStringLiteral("1")) {
        qWarning() << "[Geocode] resolveAddressSync: API 错误" << obj[QStringLiteral("info")].toString();
        return {};
    }

    QString address = obj[QStringLiteral("regeocode")]
                          .toObject()[QStringLiteral("formatted_address")]
                          .toString();
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
        qWarning() << "[Geocode] forward: Key 为空";
        return {};
    }
    if (address.trimmed().isEmpty()) {
        qWarning() << "[Geocode] forward: address 为空";
        return {};
    }

    QUrl url(QStringLiteral("https://restapi.amap.com/v3/geocode/geo"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), _geocodeApiKey);
    query.addQueryItem(QStringLiteral("address"), address.trimmed());
    if (!city.trimmed().isEmpty()) {
        query.addQueryItem(QStringLiteral("city"), city.trimmed());
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(10000);

    QNetworkReply *reply = _network->get(request);
    if (!reply) {
        qWarning() << "[Geocode] forward: get() 返回 nullptr";
        return {};
    }

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[Geocode] forward: 网络错误" << reply->errorString();
        return {};
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "[Geocode] forward: JSON 解析失败" << err.errorString();
        return {};
    }

    QJsonObject obj = doc.object();
    if (obj[QStringLiteral("status")].toString() != QStringLiteral("1")) {
        qWarning() << "[Geocode] forward: API 错误" << obj[QStringLiteral("info")].toString();
        return {};
    }

    QJsonArray geocodes = obj[QStringLiteral("geocodes")].toArray();
    if (geocodes.isEmpty()) {
        qWarning() << "[Geocode] forward: 无结果";
        return {};
    }

    QJsonObject first = geocodes.first().toObject();
    QString location = first[QStringLiteral("location")].toString();
    QString formattedAddress = first[QStringLiteral("formatted_address")].toString();

    if (location.isEmpty()) {
        qWarning() << "[Geocode] forward: location 为空";
        return {};
    }

    // location is "lng,lat" — parse into components
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

void AgentMapProvider::_onRefreshTimer()
{
    if (_geocodeApiKey.isEmpty()) {
        qWarning() << "[Geocode] timer 触发但 Key 为空, 跳过";
        return;
    }

    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        qWarning() << "[Geocode] timer 触发但无飞机, 跳过";
        return;
    }

    QGeoCoordinate coord = v->coordinate();
    if (!coord.isValid()) {
        qWarning() << "[Geocode] timer 触发但坐标无效, 跳过";
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
        qWarning() << "[Geocode] Key 为空, 无法触发查询";
        return;
    }

    _lastLat = lat;
    _lastLon = lon;

    QUrl url(QStringLiteral("https://restapi.amap.com/v3/geocode/regeo"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), _geocodeApiKey);
    query.addQueryItem(QStringLiteral("location"), QStringLiteral("%1,%2").arg(lon, 0, 'f', 6).arg(lat, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("radius"), QStringLiteral("500"));
    query.addQueryItem(QStringLiteral("extensions"), QStringLiteral("base"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(10000);

    QNetworkReply *reply = _network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[Geocode] API 请求失败:" << reply->errorString();
            return;
        }

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
        if (err.error != QJsonParseError::NoError) {
            qWarning() << "[Geocode] 响应解析失败";
            return;
        }

        QJsonObject obj = doc.object();
        if (obj[QStringLiteral("status")].toString() != QStringLiteral("1")) {
            QString info = obj[QStringLiteral("info")].toString();
            qWarning() << "[Geocode] API 返回错误:" << info;
            return;
        }

        QString address = obj[QStringLiteral("regeocode")]
                              .toObject()[QStringLiteral("formatted_address")]
                              .toString();
        if (!address.isEmpty()) {
            if (_cachedAddress != address) {
                _cachedAddress = address;
                _lastFetchTime.start();
                qWarning() << "[Geocode] 地址已更新:" << _cachedAddress;
                emit cachedAddressChanged();
            }
        }
    });
}

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
