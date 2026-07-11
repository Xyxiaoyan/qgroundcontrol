#include "WeatherTool.h"
#include "../ContextProvider.h"
#include "../AgentMapProvider.h"

#include <QtCore/QJsonDocument>

WeatherTool::WeatherTool(ContextProvider *ctx, QObject *parent)
    : AgentTool(parent)
    , _ctx(ctx)
{
}

QString WeatherTool::description() const
{
    return QStringLiteral(
        "Weather query tool using Amap (Gaode Maps) weather API. "
        "Queries current weather or weather forecast for a Chinese city. "
        "Supports two modes:\n"
        "- extensions=\"base\": returns current weather conditions (temperature, weather, "
        "wind direction/power, humidity). This is the default.\n"
        "- extensions=\"all\": returns detailed forecast data including today and upcoming days.\n"
        "The 'city' parameter can be a city name (e.g. '北京') or an adcode (e.g. '110000'). "
        "Use when user asks about weather conditions that may affect drone flight safety."
    );
}

QJsonObject WeatherTool::parametersSchema() const
{
    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");

    QJsonObject props;

    QJsonObject cityProp;
    cityProp[QStringLiteral("type")] = QStringLiteral("string");
    cityProp[QStringLiteral("description")] = QStringLiteral(
        "City name or adcode. Examples: '北京' (Beijing), '上海' (Shanghai), '深圳' (Shenzhen), "
        "or adcode like '110000' for Beijing, '310000' for Shanghai."
    );
    props[QStringLiteral("city")] = cityProp;

    QJsonObject extProp;
    extProp[QStringLiteral("type")] = QStringLiteral("string");
    extProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("base"), QStringLiteral("all")};
    extProp[QStringLiteral("description")] = QStringLiteral(
        "extensions='base' for current weather (default), extensions='all' for detailed forecast."
    );
    props[QStringLiteral("extensions")] = extProp;

    schema[QStringLiteral("properties")] = props;

    QJsonArray required;
    required.append(QStringLiteral("city"));
    schema[QStringLiteral("required")] = required;

    return schema;
}

QJsonObject WeatherTool::execute(const QJsonObject &parameters)
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

    QString city = parameters.value(QStringLiteral("city")).toString();
    QString extensions = parameters.value(QStringLiteral("extensions")).toString(QStringLiteral("base"));

    if (city.trimmed().isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("'city' parameter is required");
        return result;
    }

    QString json = map->queryWeather(city, extensions);
    if (json.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral(
            "Weather query failed. Possible causes: invalid city name, network error, "
            "or Amap API key not configured.");
        return result;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        result[QStringLiteral("error")] = QStringLiteral("Failed to parse weather response");
        return result;
    }

    return doc.object();
}
