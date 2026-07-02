#include "LocationTool.h"
#include "../ContextProvider.h"
#include "../AgentMapProvider.h"

#include <QtCore/QJsonDocument>

LocationTool::LocationTool(ContextProvider *ctx, QObject *parent)
    : AgentTool(parent)
    , _ctx(ctx)
{
}

QString LocationTool::description() const
{
    return QStringLiteral(
        "Geocoding tool — convert between place names and GPS coordinates. "
        "Two modes:\n"
        "- mode=\"reverse\" (default): reverse geocode the drone's current GPS "
        "position to a detailed Chinese address (street, district, city, province). "
        "Use when user asks \"where am I\" or needs a location description.\n"
        "- mode=\"forward\": forward geocode a place name or address to GPS "
        "coordinates. Requires the \"address\" parameter. Optionally pass \"city\" "
        "to narrow results. Use when the user says a place name and you need "
        "GPS coordinates for navigation."
    );
}

QJsonObject LocationTool::parametersSchema() const
{
    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");

    QJsonObject props;
    QJsonObject modeProp;
    modeProp[QStringLiteral("type")] = QStringLiteral("string");
    modeProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("reverse"), QStringLiteral("forward")};
    modeProp[QStringLiteral("description")] = QStringLiteral("Geocoding mode: reverse (GPS→address) or forward (address→GPS). Default: reverse.");
    props[QStringLiteral("mode")] = modeProp;

    QJsonObject addrProp;
    addrProp[QStringLiteral("type")] = QStringLiteral("string");
    addrProp[QStringLiteral("description")] = QStringLiteral("Place name or address for forward geocoding. Required when mode=forward.");
    props[QStringLiteral("address")] = addrProp;

    QJsonObject cityProp;
    cityProp[QStringLiteral("type")] = QStringLiteral("string");
    cityProp[QStringLiteral("description")] = QStringLiteral("City name to narrow forward geocode results (optional).");
    props[QStringLiteral("city")] = cityProp;

    schema[QStringLiteral("properties")] = props;
    schema[QStringLiteral("required")] = QJsonArray{};
    return schema;
}

QJsonObject LocationTool::execute(const QJsonObject &parameters)
{
    QJsonObject result;

    if (!_ctx) {
        result[QStringLiteral("error")] = QStringLiteral("Context provider unavailable");
        return result;
    }

    AgentMapProvider *map = _ctx->mapProvider();
    if (!map) {
        result[QStringLiteral("error")] = QStringLiteral("Map provider unavailable");
        return result;
    }

    QString mode = parameters.value(QStringLiteral("mode")).toString(QStringLiteral("reverse"));

    if (mode == QStringLiteral("forward")) {
        QString address = parameters.value(QStringLiteral("address")).toString();
        if (address.trimmed().isEmpty()) {
            result[QStringLiteral("error")] = QStringLiteral("forward mode requires 'address' parameter");
            return result;
        }
        QString city = parameters.value(QStringLiteral("city")).toString();

        QString json = map->resolveForwardGeocode(address, city);
        if (json.isEmpty()) {
            result[QStringLiteral("error")] = QStringLiteral(
                "无法解析该地址的坐标。可能原因：地址不明确、网络不通、"
                "或该地点不在高德地图覆盖范围内。");
            return result;
        }

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError) {
            return doc.object();
        }
        return result;
    }

    // Default: reverse geocode
    QString address = map->resolveAddressSync();
    if (address.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral(
            "无法解析当前坐标的地址。可能原因：未配置高德 API Key、网络不通、"
            "或当前位置不在高德地图覆盖范围内。");
        return result;
    }

    result[QStringLiteral("status")] = QStringLiteral("success");
    result[QStringLiteral("address")] = address;
    result[QStringLiteral("context")] = QStringLiteral("当前位置: %1").arg(address);
    result[QStringLiteral("result")] = QStringLiteral("当前位置: %1").arg(address);
    return result;
}
