#include "CommandEngine.h"
#include "Vehicle.h"
#include "Fact.h"
#include "ParameterManager.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QJsonObject>
#include <QtPositioning/QGeoCoordinate>

#include <all/all.h>

QGC_LOGGING_CATEGORY(CommandLog, "qgc.agent.cmd")

// MAV_CMD name → enum lookup
static const QHash<QString, MAV_CMD> kMavCmdMap = {
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
    {QStringLiteral("SET_GUIDED_SUBMODE_STANDARD"), MAV_CMD_SET_GUIDED_SUBMODE_STANDARD},
    {QStringLiteral("SET_GUIDED_SUBMODE_CIRCLE"),   MAV_CMD_SET_GUIDED_SUBMODE_CIRCLE},
    // Mode & Arm
    {QStringLiteral("DO_SET_MODE"),              MAV_CMD_DO_SET_MODE},
    {QStringLiteral("COMPONENT_ARM_DISARM"),     MAV_CMD_COMPONENT_ARM_DISARM},
    {QStringLiteral("DO_SET_STANDARD_MODE"),     MAV_CMD_DO_SET_STANDARD_MODE},
    {QStringLiteral("DO_FLIGHTTERMINATION"),     MAV_CMD_DO_FLIGHTTERMINATION},
    {QStringLiteral("DO_PAUSE_CONTINUE"),        MAV_CMD_DO_PAUSE_CONTINUE},
    // Camera
    {QStringLiteral("IMAGE_START_CAPTURE"),      MAV_CMD_IMAGE_START_CAPTURE},
    {QStringLiteral("IMAGE_STOP_CAPTURE"),       MAV_CMD_IMAGE_STOP_CAPTURE},
    {QStringLiteral("VIDEO_START_CAPTURE"),      MAV_CMD_VIDEO_START_CAPTURE},
    {QStringLiteral("VIDEO_STOP_CAPTURE"),       MAV_CMD_VIDEO_STOP_CAPTURE},
    {QStringLiteral("SET_CAMERA_ZOOM"),          MAV_CMD_SET_CAMERA_ZOOM},
    {QStringLiteral("SET_CAMERA_FOCUS"),         MAV_CMD_SET_CAMERA_FOCUS},
    {QStringLiteral("SET_CAMERA_MODE"),          MAV_CMD_SET_CAMERA_MODE},
    {QStringLiteral("SET_CAMERA_SOURCE"),        MAV_CMD_SET_CAMERA_SOURCE},
    {QStringLiteral("DO_DIGICAM_CONTROL"),       MAV_CMD_DO_DIGICAM_CONTROL},
    {QStringLiteral("CAMERA_TRACK_POINT"),       MAV_CMD_CAMERA_TRACK_POINT},
    {QStringLiteral("CAMERA_TRACK_RECTANGLE"),   MAV_CMD_CAMERA_TRACK_RECTANGLE},
    {QStringLiteral("CAMERA_STOP_TRACKING"),     MAV_CMD_CAMERA_STOP_TRACKING},
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
    {QStringLiteral("DO_SET_ROI_LOCATION"),      MAV_CMD_DO_SET_ROI_LOCATION},
    {QStringLiteral("DO_SET_ROI_NONE"),          MAV_CMD_DO_SET_ROI_NONE},
    {QStringLiteral("DO_SET_SERVO"),             MAV_CMD_DO_SET_SERVO},
    {QStringLiteral("DO_SET_RELAY"),             MAV_CMD_DO_SET_RELAY},
    {QStringLiteral("DO_SET_REVERSE"),           MAV_CMD_DO_SET_REVERSE},
    {QStringLiteral("DO_MOTOR_TEST"),            MAV_CMD_DO_MOTOR_TEST},
    // Misc
    {QStringLiteral("DO_SET_SAFETY_SWITCH_STATE"), MAV_CMD_DO_SET_SAFETY_SWITCH_STATE},
    {QStringLiteral("DO_FENCE_ENABLE"),          MAV_CMD_DO_FENCE_ENABLE},
    {QStringLiteral("DO_AUTOTUNE_ENABLE"),       MAV_CMD_DO_AUTOTUNE_ENABLE},
    {QStringLiteral("DO_VTOL_TRANSITION"),       MAV_CMD_DO_VTOL_TRANSITION},
    {QStringLiteral("DO_INVERTED_FLIGHT"),       MAV_CMD_DO_INVERTED_FLIGHT},
    {QStringLiteral("DO_GRIPPER"),               MAV_CMD_DO_GRIPPER},
    {QStringLiteral("DO_WINCH"),                 MAV_CMD_DO_WINCH},
    {QStringLiteral("DO_PARACHUTE"),             MAV_CMD_DO_PARACHUTE},
    {QStringLiteral("DO_ENGINE_CONTROL"),        MAV_CMD_DO_ENGINE_CONTROL},
    {QStringLiteral("PREFLIGHT_CALIBRATION"),    MAV_CMD_PREFLIGHT_CALIBRATION},
    {QStringLiteral("PREFLIGHT_REBOOT_SHUTDOWN"), MAV_CMD_PREFLIGHT_REBOOT_SHUTDOWN},
    {QStringLiteral("PREFLIGHT_STORAGE"),        MAV_CMD_PREFLIGHT_STORAGE},
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

CommandEngine *CommandEngine::instance()
{
    static CommandEngine *_inst = new CommandEngine();
    return _inst;
}

CommandEngine::CommandEngine(QObject *parent) : QObject(parent) {}

AgentCommand CommandEngine::parseCommand(const QString &text) const
{
    AgentCommand cmd;
    static const QRegularExpression cmdRe(QStringLiteral("<cmd>([^<]+)</cmd>"), QRegularExpression::CaseInsensitiveOption);
    auto it = cmdRe.globalMatch(text);

    static const QStringList validActions = {
        QStringLiteral("takeoff"), QStringLiteral("land"), QStringLiteral("rtl"),
        QStringLiteral("pause"), QStringLiteral("alt"), QStringLiteral("speed"),
        QStringLiteral("goto"), QStringLiteral("orbit"),
        QStringLiteral("fwd"), QStringLiteral("back"), QStringLiteral("left"), QStringLiteral("right"),
        QStringLiteral("turn"), QStringLiteral("mode"),
        QStringLiteral("param"), QStringLiteral("paramq"), QStringLiteral("paramlist"),
        QStringLiteral("mav"), QStringLiteral("mavint"),
    };

    while (it.hasNext()) {
        auto match = it.next();
        QString raw = match.captured(1).trimmed();
        QStringList parts = raw.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;

        QString action = parts.takeFirst().toLower();
        if (!validActions.contains(action)) continue;

        QList<double> params;

        if (action == QStringLiteral("mav") || action == QStringLiteral("mavint")) {
            // Format: mav <CMD_NAME|CMD_NUM> [p1]..[p7]
            if (parts.isEmpty()) continue;
            QString cmdId = parts.takeFirst();

            // Resolve command identifier (name or number) to MAV_CMD enum
            bool isNum = false;
            int cmdNum = cmdId.toInt(&isNum);
            if (!isNum) {
                // Try lookup by short name (e.g. "NAV_LAND") or full name ("MAV_CMD_NAV_LAND")
                QString lookup = cmdId.toUpper();
                if (lookup.startsWith(QStringLiteral("MAV_CMD_"))) {
                    lookup = lookup.mid(8); // Strip MAV_CMD_ prefix
                }
                auto mapIt = kMavCmdMap.constFind(lookup);
                cmdNum = (mapIt != kMavCmdMap.constEnd()) ? static_cast<int>(*mapIt) : -1;
            }
            if (cmdNum < 0) continue; // Unknown command

            params.append(static_cast<double>(cmdNum)); // params[0] = MAV_CMD enum value
            for (int i = 0; i < 7 && !parts.isEmpty(); i++) {
                bool ok;
                double v = parts.takeFirst().toDouble(&ok);
                params.append(ok ? v : 0.0);
            }
        } else {
            for (const auto &p : parts) {
                bool ok;
                double v = p.toDouble(&ok);
                params.append(ok ? v : 0);
            }
        }

        cmd.action = action;
        cmd.params = params;
        cmd.raw = raw;
        cmd.isValid = true;
        cmd.isReadOnly = (action == QStringLiteral("paramq") || action == QStringLiteral("paramlist"));
        break; // First valid command wins
    }
    return cmd;
}

QString CommandEngine::describeCommand(const AgentCommand &cmd) const
{
    if (!cmd.isValid) return {};

    static const QHash<QString, QString> labels = {
        {"takeoff",    QStringLiteral("起飞")},
        {"land",       QStringLiteral("降落")},
        {"rtl",        QStringLiteral("返航")},
        {"pause",      QStringLiteral("悬停")},
        {"alt",        QStringLiteral("改变高度")},
        {"speed",      QStringLiteral("改变速度")},
        {"goto",       QStringLiteral("飞向坐标")},
        {"orbit",      QStringLiteral("环绕")},
        {"fwd",        QStringLiteral("前进")},
        {"back",       QStringLiteral("后退")},
        {"left",       QStringLiteral("左移")},
        {"right",      QStringLiteral("右移")},
        {"turn",       QStringLiteral("转向")},
        {"mode",       QStringLiteral("⚠ 切换模式")},
        {"param",      QStringLiteral("⚠ 修改参数")},
        {"paramq",     QStringLiteral("查询参数")},
        {"paramlist",  QStringLiteral("列举参数")},
        {"mav",        QStringLiteral("MAVLink 指令")},
        {"mavint",     QStringLiteral("MAVLink 指令 (INT)")},
    };

    QString desc = labels.value(cmd.action, cmd.action);
    if (!cmd.params.isEmpty()) {
        desc += QStringLiteral(" (%1)").arg(cmd.raw);
    }
    return desc;
}

bool CommandEngine::executeCommand(const AgentCommand &cmd)
{
    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        emit commandFailed(QStringLiteral("无飞行器连接"));
        return false;
    }

    auto safe = [&](int idx, double def) { return _safeParam(cmd.params, idx, def); };

    try {
        if (cmd.action == QStringLiteral("takeoff")) {
            v->guidedModeTakeoff(safe(0, 5));
        } else if (cmd.action == QStringLiteral("land")) {
            v->guidedModeLand();
        } else if (cmd.action == QStringLiteral("rtl")) {
            v->guidedModeRTL(false);
        } else if (cmd.action == QStringLiteral("pause")) {
            v->pauseVehicle();
        } else if (cmd.action == QStringLiteral("alt")) {
            v->guidedModeChangeAltitude(safe(0, 0), true);
        } else if (cmd.action == QStringLiteral("speed")) {
            v->guidedModeChangeGroundSpeedMetersSecond(safe(0, 5));
        } else if (cmd.action == QStringLiteral("goto")) {
            double lat = safe(0, 0), lon = safe(1, 0), alt = safe(2, 50);
            v->guidedModeGotoLocation(QGeoCoordinate(lat, lon, alt));
        } else if (cmd.action == QStringLiteral("orbit")) {
            double lat = safe(0, 0), lon = safe(1, 0), alt = safe(2, 50), r = safe(3, 30);
            v->guidedModeOrbit(QGeoCoordinate(lat, lon, alt), r, alt);
        } else if (cmd.action == QStringLiteral("fwd")
                   || cmd.action == QStringLiteral("back")
                   || cmd.action == QStringLiteral("left")
                   || cmd.action == QStringLiteral("right")) {
            double meters = safe(0, 5);
            QGeoCoordinate pos = v->coordinate();
            double heading = 0;
            auto *hFact = v->heading();
            if (hFact) heading = hFact->rawValue().toDouble();
            // fwd=0° rel, back=180°, right=-90°, left=+90°
            if (cmd.action == QStringLiteral("back")) heading += 180;
            else if (cmd.action == QStringLiteral("right")) heading -= 90;
            else if (cmd.action == QStringLiteral("left")) heading += 90;
            // Normalize heading
            while (heading < 0) heading += 360;
            while (heading >= 360) heading -= 360;
            // Calculate target: 1° lat ≈ 111320m, 1° lon ≈ 111320 * cos(lat)
            double radH = heading * M_PI / 180.0;
            double radLat = pos.latitude() * M_PI / 180.0;
            double dLat = meters * cos(radH) / 111320.0;
            double dLon = meters * sin(radH) / (111320.0 * cos(radLat));
            QGeoCoordinate target(pos.latitude() + dLat, pos.longitude() + dLon, pos.altitude());
            v->guidedModeGotoLocation(target);
        } else if (cmd.action == QStringLiteral("turn")) {
            double degrees = safe(0, 30);
            QGeoCoordinate pos = v->coordinate();
            double heading = 0;
            auto *hFact = v->heading();
            if (hFact) heading = hFact->rawValue().toDouble();
            heading += degrees;
            while (heading < 0) heading += 360;
            while (heading >= 360) heading -= 360;
            // Use goto with current position to just change heading
            double radH = heading * M_PI / 180.0;
            double radLat = pos.latitude() * M_PI / 180.0;
            // Move 1m forward to trigger heading change
            double dLat = cos(radH) / 111320.0;
            double dLon = sin(radH) / (111320.0 * cos(radLat));
            QGeoCoordinate target(pos.latitude() + dLat, pos.longitude() + dLon, pos.altitude());
            v->guidedModeGotoLocation(target);
        } else if (cmd.action == QStringLiteral("mode")) {
            QString modeName = cmd.raw.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).value(1).toUpper();
            if (modeName.isEmpty()) { emit commandFailed(QStringLiteral("模式名为空")); return false; }
            v->setFlightMode(modeName);
            emit commandExecuted(QStringLiteral("✓ 切换模式: %1").arg(modeName));
            return true;
        } else if (cmd.action == QStringLiteral("param")) {
            auto *pm = v->parameterManager();
            if (!pm) { emit commandFailed(QStringLiteral("参数系统未就绪")); return false; }
            QString name = cmd.raw.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).value(1);
            if (name.isEmpty()) { emit commandFailed(QStringLiteral("参数名为空")); return false; }
            auto *f = pm->getParameter(-1, name);
            if (!f) { emit commandFailed(QStringLiteral("参数 %1 不存在").arg(name)); return false; }
            f->setRawValue(safe(1, 0));
            emit commandExecuted(QStringLiteral("✓ %1 = %2").arg(name).arg(safe(1, 0)));
            return true;
        } else if (cmd.action == QStringLiteral("paramq")) {
            auto *pm = v->parameterManager();
            if (!pm) { emit commandFailed(QStringLiteral("参数系统未就绪")); return false; }
            QString name = cmd.raw.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).value(1);
            auto *f = pm->getParameter(-1, name);
            QString val = f ? f->rawValue().toString() : QStringLiteral("not found");
            emit commandExecuted(QStringLiteral("参数 %1 = %2").arg(name, val));
            return true;
        } else if (cmd.action == QStringLiteral("paramlist")) {
            auto *pm = v->parameterManager();
            if (!pm) { emit commandFailed(QStringLiteral("参数系统未就绪")); return false; }
            QString pat = cmd.raw.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).value(1).toUpper();
            // Keyword aliases
            static const QHash<QString, QStringList> aliases = {
                {QStringLiteral("PID"), {QStringLiteral("_P"), QStringLiteral("_I"), QStringLiteral("_D")}},
                {QStringLiteral("RATE"), {QStringLiteral("RAT_"), QStringLiteral("RATE_")}},
                {QStringLiteral("POS"), {QStringLiteral("MPC_"), QStringLiteral("POS_")}},
                {QStringLiteral("BAT"), {QStringLiteral("BAT_")}},
                {QStringLiteral("GPS"), {QStringLiteral("GPS_")}},
                {QStringLiteral("RC"), {QStringLiteral("RC_"), QStringLiteral("COM_RC")}},
                {QStringLiteral("EKF"), {QStringLiteral("EKF2_"), QStringLiteral("EKF3_")}},
                {QStringLiteral("ARM"), {QStringLiteral("ARMING_"), QStringLiteral("COM_ARM")}},
            };
            QStringList pats = aliases.value(pat, {pat});
            QStringList names = pm->parameterNames(-1);
            QStringList out;
            for (const auto &n : names) {
                bool hit = false;
                for (const auto &p : pats) {
                    if (n.toUpper().contains(p)) { hit = true; break; }
                }
                if (hit) {
                    auto *f = pm->getParameter(-1, n);
                    out.append(QStringLiteral("%1=%2").arg(n).arg(f ? f->rawValue().toString() : QStringLiteral("?")));
                    if (out.size() >= 30) break;
                }
            }
            emit commandExecuted(out.isEmpty() ? QStringLiteral("无匹配参数") : out.join(QStringLiteral("\n")));
            return true;
        } else if (cmd.action == QStringLiteral("mav")) {
            int mavCmd = static_cast<int>(safe(0, -1));
            if (mavCmd < 0) { emit commandFailed(QStringLiteral("无效的 MAV_CMD")); return false; }
            float p1 = static_cast<float>(safe(1, 0));
            float p2 = static_cast<float>(safe(2, 0));
            float p3 = static_cast<float>(safe(3, 0));
            float p4 = static_cast<float>(safe(4, 0));
            float p5 = static_cast<float>(safe(5, 0));
            float p6 = static_cast<float>(safe(6, 0));
            float p7 = static_cast<float>(safe(7, 0));
            v->sendMavCommand(MAV_COMP_ID_AUTOPILOT1, static_cast<MAV_CMD>(mavCmd), true,
                              p1, p2, p3, p4, p5, p6, p7);
            emit commandExecuted(QStringLiteral("✓ MAV_CMD %1 sent").arg(mavCmd));
            return true;
        } else if (cmd.action == QStringLiteral("mavint")) {
            int mavCmd = static_cast<int>(safe(0, -1));
            if (mavCmd < 0) { emit commandFailed(QStringLiteral("无效的 MAV_CMD")); return false; }
            float p1 = static_cast<float>(safe(1, 0));
            float p2 = static_cast<float>(safe(2, 0));
            float p3 = static_cast<float>(safe(3, 0));
            float p4 = static_cast<float>(safe(4, 0));
            double p5 = safe(5, 0); // Latitude
            double p6 = safe(6, 0); // Longitude
            float p7 = static_cast<float>(safe(7, 0));
            v->sendMavCommandInt(MAV_COMP_ID_AUTOPILOT1, static_cast<MAV_CMD>(mavCmd),
                                 MAV_FRAME_GLOBAL, true,
                                 p1, p2, p3, p4, p5, p6, p7);
            emit commandExecuted(QStringLiteral("✓ MAV_CMD %1 (INT) sent").arg(mavCmd));
            return true;
        } else {
            emit commandFailed(QStringLiteral("未知指令"));
            return false;
        }

        emit commandExecuted(QStringLiteral("✓ 已执行: %1").arg(describeCommand(cmd)));
        return true;
    } catch (const std::exception &e) {
        emit commandFailed(QStringLiteral("异常: %1").arg(e.what()));
        return false;
    }
}

double CommandEngine::_safeParam(const QList<double> &params, int index, double def) const
{
    if (index < 0 || index >= params.size()) return def;
    double v = params[index];
    return qIsNaN(v) ? def : v;
}
