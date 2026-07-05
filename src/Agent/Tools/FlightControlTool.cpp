#include "FlightControlTool.h"
#include "Vehicle.h"
#include "MultiVehicleManager.h"

#include <QtPositioning/QGeoCoordinate>

FlightControlTool::FlightControlTool(QObject *parent)
    : AgentTool(parent)
{
}

QString FlightControlTool::description() const
{
    return QStringLiteral(
        "Execute flight control commands: takeoff, land, return-to-launch, pause/hover, "
        "goto a GPS coordinate, or orbit around a point. Requires GPS lock for goto/orbit."
    );
}

QJsonObject FlightControlTool::parametersSchema() const
{
    QJsonObject props;

    QJsonObject action;
    action[QStringLiteral("type")] = QStringLiteral("string");
    action[QStringLiteral("description")] = QStringLiteral("The flight control action to execute");
    QJsonArray actionEnum = {QStringLiteral("takeoff"), QStringLiteral("land"), QStringLiteral("rtl"),
                             QStringLiteral("pause"), QStringLiteral("goto"), QStringLiteral("orbit")};
    action[QStringLiteral("enum")] = actionEnum;
    props[QStringLiteral("action")] = action;

    QJsonObject param1;
    param1[QStringLiteral("type")] = QStringLiteral("number");
    param1[QStringLiteral("description")] = QStringLiteral("Altitude (m) for takeoff (default 5); Latitude for goto/orbit");
    props[QStringLiteral("param1")] = param1;

    QJsonObject param2;
    param2[QStringLiteral("type")] = QStringLiteral("number");
    param2[QStringLiteral("description")] = QStringLiteral("Longitude for goto/orbit");
    props[QStringLiteral("param2")] = param2;

    QJsonObject param3;
    param3[QStringLiteral("type")] = QStringLiteral("number");
    param3[QStringLiteral("description")] = QStringLiteral("Altitude (m) for goto/orbit (default 50)");
    props[QStringLiteral("param3")] = param3;

    QJsonObject param4;
    param4[QStringLiteral("type")] = QStringLiteral("number");
    param4[QStringLiteral("description")] = QStringLiteral("Radius (m) for orbit (default 30)");
    props[QStringLiteral("param4")] = param4;

    QJsonArray required;
    required.append(QStringLiteral("action"));

    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = props;
    schema[QStringLiteral("required")] = required;
    return schema;
}

static double safeParam(const QJsonObject &p, const QString &key, double def)
{
    if (!p.contains(key)) return def;
    double v = p[key].toDouble(def);
    return qIsNaN(v) ? def : v;
}

QString FlightControlTool::preExecutionCheck(const QJsonObject &parameters, Vehicle *vehicle) const
{
    if (!vehicle) return QStringLiteral("No vehicle connected");
    QString action = parameters[QStringLiteral("action")].toString();
    if (action == QStringLiteral("takeoff")) {
        if (!vehicle->readyToFly()) return QStringLiteral("Vehicle not ready to fly");
        if (vehicle->armed()) return QStringLiteral("Vehicle already armed");
    }
    return {};
}

QJsonObject FlightControlTool::execute(const QJsonObject &parameters)
{
    QJsonObject result;
    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        result[QStringLiteral("error")] = QStringLiteral("No vehicle connected");
        result[QStringLiteral("status")] = QStringLiteral("error");
        return result;
    }

    QString action = parameters[QStringLiteral("action")].toString();

    // Actions that require guided mode
    static const QStringList guidedActions = {
        QStringLiteral("takeoff"), QStringLiteral("goto"), QStringLiteral("orbit"), QStringLiteral("pause")
    };
    if (guidedActions.contains(action) && !v->guidedMode()) {
        result[QStringLiteral("error")] = QStringLiteral(
            "Vehicle is not in guided mode (current: %1). "
            "Use flight_mode tool to switch to a guided-capable mode first "
            "(PX4: POSCTL or OFFBOARD; ArduPilot: GUIDED).")
            .arg(v->flightMode());
        result[QStringLiteral("status")] = QStringLiteral("error");
        return result;
    }

    if (action == QStringLiteral("takeoff")) {
        double alt = safeParam(parameters, QStringLiteral("param1"), 5);
        if (!v->armed() && !v->readyToFly()) {
            result[QStringLiteral("error")] = QStringLiteral("Vehicle not ready to fly. Check pre-arm checks and GPS lock.");
            result[QStringLiteral("status")] = QStringLiteral("error");
            return result;
        }
        v->guidedModeTakeoff(alt);
        result[QStringLiteral("result")] = QStringLiteral("Takeoff to %1m").arg(alt);
    } else if (action == QStringLiteral("land")) {
        v->guidedModeLand();
        result[QStringLiteral("result")] = QStringLiteral("Landing");
    } else if (action == QStringLiteral("rtl")) {
        v->guidedModeRTL(false);
        result[QStringLiteral("result")] = QStringLiteral("Returning to launch");
    } else if (action == QStringLiteral("pause")) {
        v->pauseVehicle();
        result[QStringLiteral("result")] = QStringLiteral("Hovering at current position");
    } else if (action == QStringLiteral("goto")) {
        double lat = safeParam(parameters, QStringLiteral("param1"), 0);
        double lon = safeParam(parameters, QStringLiteral("param2"), 0);
        double alt = safeParam(parameters, QStringLiteral("param3"), 50);
        QGeoCoordinate target(lat, lon, alt);
        if (!target.isValid()) {
            result[QStringLiteral("error")] = QStringLiteral("Invalid goto coordinates");
            result[QStringLiteral("status")] = QStringLiteral("error");
            return result;
        }
        if (!v->guidedModeGotoLocation(target)) {
            result[QStringLiteral("error")] = QStringLiteral(
                "Goto command rejected. Target may be too far or GPS invalid.");
            result[QStringLiteral("status")] = QStringLiteral("error");
            return result;
        }
        result[QStringLiteral("result")] = QStringLiteral("Flying to (%1, %2) at %3m")
            .arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(alt);
    } else if (action == QStringLiteral("orbit")) {
        double lat = safeParam(parameters, QStringLiteral("param1"), 0);
        double lon = safeParam(parameters, QStringLiteral("param2"), 0);
        double alt = safeParam(parameters, QStringLiteral("param3"), 50);
        double r = safeParam(parameters, QStringLiteral("param4"), 30);
        QGeoCoordinate center(lat, lon, alt);
        if (!center.isValid()) {
            result[QStringLiteral("error")] = QStringLiteral("Invalid orbit center coordinates");
            result[QStringLiteral("status")] = QStringLiteral("error");
            return result;
        }
        v->guidedModeOrbit(center, r, alt);
        result[QStringLiteral("result")] = QStringLiteral("Orbiting (%1, %2) r=%3m alt=%4m")
            .arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(r).arg(alt);
    } else {
        result[QStringLiteral("error")] = QStringLiteral("Unknown action: %1").arg(action);
        result[QStringLiteral("status")] = QStringLiteral("error");
        return result;
    }

    result[QStringLiteral("status")] = QStringLiteral("success");
    return result;
}
