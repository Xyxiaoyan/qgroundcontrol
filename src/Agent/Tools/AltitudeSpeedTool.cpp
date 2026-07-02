#include "AltitudeSpeedTool.h"
#include "Vehicle.h"
#include "MultiVehicleManager.h"

AltitudeSpeedTool::AltitudeSpeedTool(QObject *parent)
    : AgentTool(parent)
{
}

QString AltitudeSpeedTool::description() const
{
    return QStringLiteral(
        "Change the vehicle's altitude or ground speed. "
        "Altitude changes are relative (positive=climb, negative=descend). "
        "Speed sets absolute ground speed in m/s."
    );
}

QJsonObject AltitudeSpeedTool::parametersSchema() const
{
    QJsonObject props;

    QJsonObject param;
    param[QStringLiteral("type")] = QStringLiteral("string");
    param[QStringLiteral("description")] = QStringLiteral("What to change: altitude or speed");
    QJsonArray paramEnum = {QStringLiteral("altitude"), QStringLiteral("speed")};
    param[QStringLiteral("enum")] = paramEnum;
    props[QStringLiteral("parameter")] = param;

    QJsonObject value;
    value[QStringLiteral("type")] = QStringLiteral("number");
    value[QStringLiteral("description")] = QStringLiteral("Value in meters (altitude, relative) or m/s (speed, absolute)");
    props[QStringLiteral("value")] = value;

    QJsonArray required;
    required.append(QStringLiteral("parameter"));
    required.append(QStringLiteral("value"));

    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = props;
    schema[QStringLiteral("required")] = required;
    return schema;
}

QJsonObject AltitudeSpeedTool::execute(const QJsonObject &parameters)
{
    QJsonObject result;
    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        result[QStringLiteral("error")] = QStringLiteral("No vehicle connected");
        return result;
    }

    QString param = parameters[QStringLiteral("parameter")].toString();
    double value = parameters[QStringLiteral("value")].toDouble();

    try {
        if (param == QStringLiteral("altitude")) {
            v->guidedModeChangeAltitude(value, true);
            result[QStringLiteral("result")] = QStringLiteral("✓ 高度变化 %1m").arg(value);
        } else if (param == QStringLiteral("speed")) {
            v->guidedModeChangeGroundSpeedMetersSecond(value);
            result[QStringLiteral("result")] = QStringLiteral("✓ 地速设为 %1m/s").arg(value);
        } else {
            result[QStringLiteral("error")] = QStringLiteral("Unknown parameter: %1").arg(param);
        }
    } catch (const std::exception &e) {
        result[QStringLiteral("error")] = QStringLiteral("Exception: %1").arg(e.what());
    }

    result[QStringLiteral("status")] = result.contains(QStringLiteral("error")) ? QStringLiteral("error") : QStringLiteral("success");
    return result;
}
