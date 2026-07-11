#include "RoutePlanningTool.h"
#include "../ContextProvider.h"
#include "../AgentMapProvider.h"

#include <QtCore/QJsonDocument>

RoutePlanningTool::RoutePlanningTool(ContextProvider *ctx, QObject *parent)
    : AgentTool(parent)
    , _ctx(ctx)
{
}

QString RoutePlanningTool::description() const
{
    return QStringLiteral(
        "Route planning tool using Amap (Gaode Maps) driving direction API. "
        "Plans a driving route between two GPS coordinates. "
        "Both origin and destination must be in 'lng,lat' format (e.g. '116.481028,39.989643'). "
        "Returns route distance (meters), duration (seconds), and step-by-step navigation instructions. "
        "Use when user asks about driving directions, route planning, or travel time estimation."
    );
}

QJsonObject RoutePlanningTool::parametersSchema() const
{
    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");

    QJsonObject props;

    QJsonObject originProp;
    originProp[QStringLiteral("type")] = QStringLiteral("string");
    originProp[QStringLiteral("description")] = QStringLiteral(
        "Origin coordinate in 'lng,lat' format (e.g. '116.481028,39.989643'). "
        "Can use forward geocoding (get_location_address tool with mode=forward) to get coordinates from a place name."
    );
    props[QStringLiteral("origin")] = originProp;

    QJsonObject destProp;
    destProp[QStringLiteral("type")] = QStringLiteral("string");
    destProp[QStringLiteral("description")] = QStringLiteral(
        "Destination coordinate in 'lng,lat' format (e.g. '116.465302,40.004717'). "
        "Can use forward geocoding (get_location_address tool with mode=forward) to get coordinates from a place name."
    );
    props[QStringLiteral("destination")] = destProp;

    QJsonObject strategyProp;
    strategyProp[QStringLiteral("type")] = QStringLiteral("string");
    strategyProp[QStringLiteral("enum")] = QJsonArray{
        QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"),
        QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5"),
        QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8"),
        QStringLiteral("9"), QStringLiteral("10")
    };
    strategyProp[QStringLiteral("description")] = QStringLiteral(
        "Route strategy: 0=fastest (default), 1=no-highways, 2=shortest distance, "
        "3=no-tolls, 4=avoid congestion+no-highways, 5=avoid congestion+no-tolls, "
        "6=avoid congestion, 7=avoid congestion+no-tolls+no-highways, "
        "8=avoid congestion+no-tolls, 9=prefer highways, 10=avoid congestion+prefer highways."
    );
    props[QStringLiteral("strategy")] = strategyProp;

    schema[QStringLiteral("properties")] = props;

    QJsonArray required;
    required.append(QStringLiteral("origin"));
    required.append(QStringLiteral("destination"));
    schema[QStringLiteral("required")] = required;

    return schema;
}

QJsonObject RoutePlanningTool::execute(const QJsonObject &parameters)
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

    QString origin = parameters.value(QStringLiteral("origin")).toString();
    QString destination = parameters.value(QStringLiteral("destination")).toString();
    QString strategy = parameters.value(QStringLiteral("strategy")).toString();

    if (origin.trimmed().isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("'origin' parameter is required (format: 'lng,lat')");
        return result;
    }
    if (destination.trimmed().isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("'destination' parameter is required (format: 'lng,lat')");
        return result;
    }

    QString json = map->planDrivingRoute(origin, destination, strategy);
    if (json.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral(
            "Route planning failed. Possible causes: invalid coordinates, network error, "
            "or Amap API key not configured.");
        return result;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        result[QStringLiteral("error")] = QStringLiteral("Failed to parse route planning response");
        return result;
    }

    return doc.object();
}
