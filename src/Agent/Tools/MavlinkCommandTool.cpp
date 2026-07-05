#include "MavlinkCommandTool.h"
#include "Vehicle.h"
#include "MultiVehicleManager.h"

// Build MAV_CMD name→enum lookup table
QHash<QString, MAV_CMD> MavlinkCommandTool::_buildCmdMap()
{
    return {
        // Navigation
        {QStringLiteral("NAV_WAYPOINT"),             MAV_CMD_NAV_WAYPOINT},
        {QStringLiteral("NAV_LAND"),                 MAV_CMD_NAV_LAND},
        {QStringLiteral("NAV_TAKEOFF"),              MAV_CMD_NAV_TAKEOFF},
        {QStringLiteral("NAV_RETURN_TO_LAUNCH"),     MAV_CMD_NAV_RETURN_TO_LAUNCH},
        {QStringLiteral("NAV_LOITER_UNLIM"),         MAV_CMD_NAV_LOITER_UNLIM},
        {QStringLiteral("NAV_LOITER_TIME"),          MAV_CMD_NAV_LOITER_TIME},
        {QStringLiteral("NAV_LOITER_TURNS"),         MAV_CMD_NAV_LOITER_TURNS},
        {QStringLiteral("NAV_LOITER_TO_ALT"),        MAV_CMD_NAV_LOITER_TO_ALT},
        {QStringLiteral("NAV_ROI"),                  MAV_CMD_NAV_ROI},
        {QStringLiteral("NAV_FOLLOW"),               MAV_CMD_NAV_FOLLOW},
        // Guided
        {QStringLiteral("DO_REPOSITION"),            MAV_CMD_DO_REPOSITION},
        {QStringLiteral("DO_CHANGE_SPEED"),          MAV_CMD_DO_CHANGE_SPEED},
        {QStringLiteral("DO_CHANGE_ALTITUDE"),       MAV_CMD_DO_CHANGE_ALTITUDE},
        {QStringLiteral("DO_ORBIT"),                 MAV_CMD_DO_ORBIT},
        {QStringLiteral("DO_FOLLOW"),                MAV_CMD_DO_FOLLOW},
        {QStringLiteral("GUIDED_CHANGE_ALTITUDE"),   MAV_CMD_GUIDED_CHANGE_ALTITUDE},
        {QStringLiteral("GUIDED_CHANGE_SPEED"),      MAV_CMD_GUIDED_CHANGE_SPEED},
        {QStringLiteral("GUIDED_CHANGE_HEADING"),    MAV_CMD_GUIDED_CHANGE_HEADING},
        {QStringLiteral("NAV_GUIDED_ENABLE"),        MAV_CMD_NAV_GUIDED_ENABLE},
        // Mode & Arm
        {QStringLiteral("DO_SET_MODE"),              MAV_CMD_DO_SET_MODE},
        {QStringLiteral("COMPONENT_ARM_DISARM"),     MAV_CMD_COMPONENT_ARM_DISARM},
        {QStringLiteral("DO_FLIGHTTERMINATION"),     MAV_CMD_DO_FLIGHTTERMINATION},
        {QStringLiteral("DO_PAUSE_CONTINUE"),        MAV_CMD_DO_PAUSE_CONTINUE},
        // Camera
        {QStringLiteral("IMAGE_START_CAPTURE"),      MAV_CMD_IMAGE_START_CAPTURE},
        {QStringLiteral("IMAGE_STOP_CAPTURE"),       MAV_CMD_IMAGE_STOP_CAPTURE},
        {QStringLiteral("VIDEO_START_CAPTURE"),      MAV_CMD_VIDEO_START_CAPTURE},
        {QStringLiteral("VIDEO_STOP_CAPTURE"),       MAV_CMD_VIDEO_STOP_CAPTURE},
        {QStringLiteral("SET_CAMERA_ZOOM"),          MAV_CMD_SET_CAMERA_ZOOM},
        {QStringLiteral("SET_CAMERA_FOCUS"),         MAV_CMD_SET_CAMERA_FOCUS},
        {QStringLiteral("DO_DIGICAM_CONTROL"),       MAV_CMD_DO_DIGICAM_CONTROL},
        // Gimbal
        {QStringLiteral("DO_MOUNT_CONTROL"),         MAV_CMD_DO_MOUNT_CONTROL},
        {QStringLiteral("DO_MOUNT_CONFIGURE"),       MAV_CMD_DO_MOUNT_CONFIGURE},
        {QStringLiteral("DO_GIMBAL_MANAGER_PITCHYAW"), MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW},
        // Mission
        {QStringLiteral("MISSION_START"),            MAV_CMD_MISSION_START},
        {QStringLiteral("DO_JUMP"),                  MAV_CMD_DO_JUMP},
        {QStringLiteral("DO_SET_MISSION_CURRENT"),   MAV_CMD_DO_SET_MISSION_CURRENT},
        {QStringLiteral("DO_SET_HOME"),              MAV_CMD_DO_SET_HOME},
        // Parameters & settings
        {QStringLiteral("DO_SET_PARAMETER"),         MAV_CMD_DO_SET_PARAMETER},
        {QStringLiteral("DO_SET_ROI"),               MAV_CMD_DO_SET_ROI},
        {QStringLiteral("DO_SET_SERVO"),             MAV_CMD_DO_SET_SERVO},
        {QStringLiteral("DO_SET_RELAY"),             MAV_CMD_DO_SET_RELAY},
        {QStringLiteral("DO_MOTOR_TEST"),            MAV_CMD_DO_MOTOR_TEST},
        // Misc
        {QStringLiteral("DO_FENCE_ENABLE"),          MAV_CMD_DO_FENCE_ENABLE},
        {QStringLiteral("DO_AUTOTUNE_ENABLE"),       MAV_CMD_DO_AUTOTUNE_ENABLE},
        {QStringLiteral("DO_VTOL_TRANSITION"),       MAV_CMD_DO_VTOL_TRANSITION},
        {QStringLiteral("PREFLIGHT_CALIBRATION"),    MAV_CMD_PREFLIGHT_CALIBRATION},
        {QStringLiteral("PREFLIGHT_REBOOT_SHUTDOWN"), MAV_CMD_PREFLIGHT_REBOOT_SHUTDOWN},
        {QStringLiteral("RUN_PREARM_CHECKS"),        MAV_CMD_RUN_PREARM_CHECKS},
        {QStringLiteral("REQUEST_MESSAGE"),          MAV_CMD_REQUEST_MESSAGE},
        {QStringLiteral("SET_MESSAGE_INTERVAL"),     MAV_CMD_SET_MESSAGE_INTERVAL},
        {QStringLiteral("LOGGING_START"),            MAV_CMD_LOGGING_START},
        {QStringLiteral("LOGGING_STOP"),             MAV_CMD_LOGGING_STOP},
        {QStringLiteral("DO_CONTROL_VIDEO"),         MAV_CMD_DO_CONTROL_VIDEO},
        {QStringLiteral("FLASH_BOOTLOADER"),         MAV_CMD_FLASH_BOOTLOADER},
        {QStringLiteral("DO_TRIGGER_CONTROL"),       MAV_CMD_DO_TRIGGER_CONTROL},
        {QStringLiteral("DO_GO_AROUND"),             MAV_CMD_DO_GO_AROUND},
    };
}

const QHash<QString, MAV_CMD> MavlinkCommandTool::kCmdMap = MavlinkCommandTool::_buildCmdMap();

MavlinkCommandTool::MavlinkCommandTool(QObject *parent)
    : AgentTool(parent)
{
}

QString MavlinkCommandTool::description() const
{
    return QStringLiteral(
        "Send a raw MAVLink command (MAV_CMD) to the flight controller. "
        "Use this for advanced operations not covered by other tools. "
        "Valid command names include: NAV_LAND, NAV_TAKEOFF, NAV_RETURN_TO_LAUNCH, "
        "DO_SET_MODE, COMPONENT_ARM_DISARM, IMAGE_START_CAPTURE, DO_MOUNT_CONTROL, "
        "DO_SET_ROI, DO_FENCE_ENABLE, DO_MOTOR_TEST, PREFLIGHT_CALIBRATION, "
        "DO_AUTOTUNE_ENABLE, DO_VTOL_TRANSITION, and many more. "
        "Supply up to 7 float parameters (p1–p7). For GPS coordinates, use use_int=true "
        "which passes p5 as latitude and p6 as longitude as doubles via sendMavCommandInt."
    );
}

QJsonObject MavlinkCommandTool::parametersSchema() const
{
    QJsonObject props;

    QJsonObject cmdName;
    cmdName[QStringLiteral("type")] = QStringLiteral("string");
    cmdName[QStringLiteral("description")] = QStringLiteral("MAV_CMD name (e.g. NAV_LAND, DO_SET_MODE, IMAGE_START_CAPTURE)");
    props[QStringLiteral("command_name")] = cmdName;

    QJsonObject useInt;
    useInt[QStringLiteral("type")] = QStringLiteral("boolean");
    useInt[QStringLiteral("description")] = QStringLiteral("Use sendMavCommandInt (p5=lat, p6=lon as doubles). False uses sendMavCommand (all float)");
    props[QStringLiteral("use_int")] = useInt;

    QJsonObject params;
    params[QStringLiteral("type")] = QStringLiteral("array");
    params[QStringLiteral("description")] = QStringLiteral("Up to 7 float parameters p1–p7");
    QJsonObject items;
    items[QStringLiteral("type")] = QStringLiteral("number");
    params[QStringLiteral("items")] = items;
    params[QStringLiteral("minItems")] = 0;
    params[QStringLiteral("maxItems")] = 7;
    props[QStringLiteral("params")] = params;

    QJsonArray required;
    required.append(QStringLiteral("command_name"));

    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = props;
    schema[QStringLiteral("required")] = required;
    return schema;
}

QJsonObject MavlinkCommandTool::execute(const QJsonObject &parameters)
{
    QJsonObject result;
    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        result[QStringLiteral("error")] = QStringLiteral("No vehicle connected");
        return result;
    }

    QString cmdId = parameters[QStringLiteral("command_name")].toString().toUpper();
    if (cmdId.startsWith(QStringLiteral("MAV_CMD_"))) {
        cmdId = cmdId.mid(8);
    }
    auto it = kCmdMap.constFind(cmdId);
    if (it == kCmdMap.constEnd()) {
        // Try numeric
        bool ok;
        int num = cmdId.toInt(&ok);
        if (ok && num > 0) {
            it = kCmdMap.constFind(QString()); // dummy to force numeric path
        } else {
            result[QStringLiteral("error")] = QStringLiteral("Unknown MAV_CMD: %1").arg(cmdId);
            return result;
        }
    }

    MAV_CMD cmd = (it != kCmdMap.constEnd()) ? *it : static_cast<MAV_CMD>(cmdId.toInt());

    QJsonArray paramsArr = parameters[QStringLiteral("params")].toArray();
    auto p = [&](int i, float def = 0) -> float {
        if (i >= paramsArr.size()) return def;
        double val = paramsArr[i].toDouble(def);
        return qIsNaN(val) ? def : static_cast<float>(val);
    };

    bool useInt = parameters[QStringLiteral("use_int")].toBool(false);

    try {
        if (useInt) {
            double lat = (paramsArr.size() > 4) ? paramsArr[4].toDouble(0.0) : 0.0;
            double lon = (paramsArr.size() > 5) ? paramsArr[5].toDouble(0.0) : 0.0;
            v->sendMavCommandInt(MAV_COMP_ID_AUTOPILOT1, cmd, MAV_FRAME_GLOBAL, true,
                                 p(0), p(1), p(2), p(3), lat, lon, p(6));
            result[QStringLiteral("result")] = QStringLiteral("✓ MAV_CMD %1 (INT) sent").arg(cmdId);
        } else {
            v->sendMavCommand(MAV_COMP_ID_AUTOPILOT1, cmd, true,
                              p(0), p(1), p(2), p(3), p(4), p(5), p(6));
            result[QStringLiteral("result")] = QStringLiteral("✓ MAV_CMD %1 sent").arg(cmdId);
        }
    } catch (const std::exception &e) {
        result[QStringLiteral("error")] = QStringLiteral("Exception: %1").arg(e.what());
    }

    result[QStringLiteral("status")] = result.contains(QStringLiteral("error")) ? QStringLiteral("error") : QStringLiteral("success");
    return result;
}
