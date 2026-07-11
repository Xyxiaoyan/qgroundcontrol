#include "DistanceTool.h"
#include "../ContextProvider.h"
#include "../AgentMapProvider.h"

#include <QtCore/QJsonDocument>

DistanceTool::DistanceTool(ContextProvider *ctx, QObject *parent)
    : AgentTool(parent)
    , _ctx(ctx)
{
}

QString DistanceTool::description() const
{
    return QStringLiteral(
        "Distance measurement tool using Amap (Gaode Maps) distance API. "
        "Calculates the distance and estimated travel time between one or more origins "
        "and a destination. Supports three measurement types:\n"
        "- type=0: straight-line distance (default)\n"
        "- type=1: driving distance (follows road network)\n"
        "- type=3: walking distance (follows walking paths)\n"
        "Coordinate format is 'lng,lat' (e.g. '116.481028,39.989643'). "
        "Multiple origins can be separated by '|' (e.g. 'lng1,lat1|lng2,lat2'). "
        "Use when user asks about distance or travel time between locations."
    );
}

QJsonObject DistanceTool::parametersSchema() const
{
    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");

    QJsonObject props;

    QJsonObject originsProp;
    originsProp[QStringLiteral("type")] = QStringLiteral("string");
    originsProp[QStringLiteral("description")] = QStringLiteral(
        "Origin coordinate(s) in 'lng,lat' format. Multiple origins separated by '|'. "
        "Example: '116.481028,39.989643' or '116.481028,39.989643|116.465302,40.004717'."
    );
    props[QStringLiteral("origins")] = originsProp;

    QJsonObject destProp;
    destProp[QStringLiteral("type")] = QStringLiteral("string");
    destProp[QStringLiteral("description")] = QStringLiteral(
        "Destination coordinate in 'lng,lat' format. Example: '116.465302,40.004717'."
    );
    props[QStringLiteral("destination")] = destProp;

    QJsonObject typeProp;
    typeProp[QStringLiteral("type")] = QStringLiteral("string");
    typeProp[QStringLiteral("enum")] = QJsonArray{
        QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("3")
    };
    typeProp[QStringLiteral("description")] = QStringLiteral(
        "Distance type: '0' for straight-line distance (default), "
        "'1' for driving distance, '3' for walking distance."
    );
    props[QStringLiteral("type")] = typeProp;

    schema[QStringLiteral("properties")] = props;

    QJsonArray required;
    required.append(QStringLiteral("origins"));
    required.append(QStringLiteral("destination"));
    schema[QStringLiteral("required")] = required;

    return schema;
}

QJsonObject DistanceTool::execute(const QJsonObject &parameters)
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

    QString origins = parameters.value(QStringLiteral("origins")).toString();
    QString destination = parameters.value(QStringLiteral("destination")).toString();
    QString type = parameters.value(QStringLiteral("type")).toString(QStringLiteral("0"));

    if (origins.trimmed().isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("'origins' parameter is required (format: 'lng,lat')");
        return result;
    }
    if (destination.trimmed().isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("'destination' parameter is required (format: 'lng,lat')");
        return result;
    }

    QString json = map->measureDistance(origins, destination, type);
    if (json.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral(
            "Distance measurement failed. Possible causes: invalid coordinates, "
            "network error, or Amap API key not configured.");
        return result;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        result[QStringLiteral("error")] = QStringLiteral("Failed to parse distance response");
        return result;
    }

    return doc.object();
}
