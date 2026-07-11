#include "POISearchTool.h"
#include "../ContextProvider.h"
#include "../AgentMapProvider.h"

#include <QtCore/QJsonDocument>

POISearchTool::POISearchTool(ContextProvider *ctx, QObject *parent)
    : AgentTool(parent)
    , _ctx(ctx)
{
}

QString POISearchTool::description() const
{
    return QStringLiteral(
        "POI (Point of Interest) search tool using Amap (Gaode Maps). "
        "Two search modes:\n"
        "- mode=\"keyword\": search POI by keywords within a city (e.g. find restaurants, gas stations, "
        "hospitals in a city). Requires 'keywords' and 'city' parameters.\n"
        "- mode=\"around\": search POI around a specific GPS coordinate (e.g. find nearby landmarks "
        "within a radius). Requires 'keywords' and 'location' parameters. Optionally specify 'radius' "
        "(default 3000m) and 'city'.\n"
        "Location format is 'lng,lat' (e.g. '116.481028,39.989643'). "
        "Returns POI name, address, GPS coordinates, and category."
    );
}

QJsonObject POISearchTool::parametersSchema() const
{
    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");

    QJsonObject props;

    QJsonObject modeProp;
    modeProp[QStringLiteral("type")] = QStringLiteral("string");
    modeProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("keyword"), QStringLiteral("around")};
    modeProp[QStringLiteral("description")] = QStringLiteral(
        "Search mode: 'keyword' for city-wide POI search, 'around' for nearby search around a GPS point. Default: keyword."
    );
    props[QStringLiteral("mode")] = modeProp;

    QJsonObject kwProp;
    kwProp[QStringLiteral("type")] = QStringLiteral("string");
    kwProp[QStringLiteral("description")] = QStringLiteral(
        "Search keywords, e.g. '餐厅' (restaurant), '加油站' (gas station), '医院' (hospital), '停车场' (parking)."
    );
    props[QStringLiteral("keywords")] = kwProp;

    QJsonObject cityProp;
    cityProp[QStringLiteral("type")] = QStringLiteral("string");
    cityProp[QStringLiteral("description")] = QStringLiteral(
        "City name or adcode for narrowing the search. Required for keyword mode, optional for around mode."
    );
    props[QStringLiteral("city")] = cityProp;

    QJsonObject locProp;
    locProp[QStringLiteral("type")] = QStringLiteral("string");
    locProp[QStringLiteral("description")] = QStringLiteral(
        "Center point coordinate in 'lng,lat' format for around search. Required for around mode."
    );
    props[QStringLiteral("location")] = locProp;

    QJsonObject radiusProp;
    radiusProp[QStringLiteral("type")] = QStringLiteral("integer");
    radiusProp[QStringLiteral("description")] = QStringLiteral(
        "Search radius in meters for around mode. Default: 3000 (3km). Range: 0-50000."
    );
    props[QStringLiteral("radius")] = radiusProp;

    QJsonObject offsetProp;
    offsetProp[QStringLiteral("type")] = QStringLiteral("integer");
    offsetProp[QStringLiteral("description")] = QStringLiteral(
        "Number of results per page. Default: 20. Range: 1-25."
    );
    props[QStringLiteral("offset")] = offsetProp;

    schema[QStringLiteral("properties")] = props;

    QJsonArray required;
    required.append(QStringLiteral("keywords"));
    schema[QStringLiteral("required")] = required;

    return schema;
}

QJsonObject POISearchTool::execute(const QJsonObject &parameters)
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

    QString mode = parameters.value(QStringLiteral("mode")).toString(QStringLiteral("keyword"));
    QString keywords = parameters.value(QStringLiteral("keywords")).toString();
    QString city = parameters.value(QStringLiteral("city")).toString();
    QString location = parameters.value(QStringLiteral("location")).toString();
    int radius = parameters.value(QStringLiteral("radius")).toInt(3000);
    int offset = parameters.value(QStringLiteral("offset")).toInt(20);

    if (keywords.trimmed().isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("'keywords' parameter is required");
        return result;
    }

    QString json;

    if (mode == QStringLiteral("around")) {
        if (location.trimmed().isEmpty()) {
            result[QStringLiteral("error")] = QStringLiteral(
                "'location' parameter is required for around search mode (format: 'lng,lat')");
            return result;
        }
        json = map->searchPOIAround(keywords, location, radius, offset);
    } else {
        json = map->searchPOI(keywords, city, location, offset);
    }

    if (json.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral(
            "POI search returned no results. Possible causes: no matching POIs, "
            "network error, or Amap API key not configured.");
        return result;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        result[QStringLiteral("error")] = QStringLiteral("Failed to parse POI search response");
        return result;
    }

    return doc.object();
}
