#include "FlightModeTool.h"
#include "Vehicle.h"
#include "MultiVehicleManager.h"

FlightModeTool::FlightModeTool(QObject *parent)
    : AgentTool(parent)
{
}

QString FlightModeTool::description() const
{
    return QStringLiteral(
        "Switch the vehicle flight mode. The mode name depends on the flight controller firmware. "
        "Check the vehicle state first to determine firmware type, then use the appropriate mode:\n"
        "- PX4: MANUAL, ALTCTL, POSCTL, AUTO_MISSION, AUTO_LOITER, AUTO_RTL, ACRO, STABILIZED, OFFBOARD, AUTO_TAKEOFF, AUTO_LAND\n"
        "- ArduPilot Copter: GUIDED, LOITER, RTL, LAND, AUTO, STABILIZE, ALT_HOLD, POSHOLD, BRAKE, CIRCLE, SMART_RTL, AUTOTUNE\n"
        "WARNING: Changing flight mode can affect vehicle behavior significantly. Always requires user confirmation."
    );
}

QJsonObject FlightModeTool::parametersSchema() const
{
    QJsonObject props;

    QJsonObject mode;
    mode[QStringLiteral("type")] = QStringLiteral("string");
    mode[QStringLiteral("description")] = QStringLiteral(
        "Flight mode name (e.g. GUIDED, LOITER, ALTCTL, POSCTL). "
        "Check firmware type to choose the correct mode name.");
    props[QStringLiteral("mode")] = mode;

    QJsonArray required;
    required.append(QStringLiteral("mode"));

    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = props;
    schema[QStringLiteral("required")] = required;
    return schema;
}

QJsonObject FlightModeTool::execute(const QJsonObject &parameters)
{
    QJsonObject result;
    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        result[QStringLiteral("error")] = QStringLiteral("No vehicle connected");
        return result;
    }

    QString modeName = parameters[QStringLiteral("mode")].toString().toUpper().trimmed();
    if (modeName.isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("Mode name is empty");
        return result;
    }

    // Validate mode name against firmware-supported modes
    QStringList validModes = v->flightModes();
    if (!validModes.contains(modeName, Qt::CaseInsensitive)) {
        QString currentMode = v->flightMode();
        result[QStringLiteral("error")] = QStringLiteral(
            "Unknown flight mode \"%1\". Current mode: %2. Valid modes: %3")
            .arg(modeName, currentMode, validModes.join(QStringLiteral(", ")));
        result[QStringLiteral("status")] = QStringLiteral("error");
        return result;
    }

    v->setFlightMode(modeName);
    result[QStringLiteral("result")] = QStringLiteral("Sent mode switch command: %1").arg(modeName);
    result[QStringLiteral("status")] = QStringLiteral("success");
    return result;
}
