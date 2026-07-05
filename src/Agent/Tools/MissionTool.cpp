#include "MissionTool.h"
#include "Vehicle.h"
#include "MultiVehicleManager.h"
#include "MissionManager.h"
#include "MissionItem.h"

#include <limits>

MissionTool::MissionTool(QObject *parent)
    : AgentTool(parent)
{
}

MissionTool::~MissionTool() = default;

QString MissionTool::description() const
{
    return QStringLiteral(
        "Manage waypoint missions with altitude, yaw, acceptance radius, and hold time per waypoint. "
        "Build a mission locally, then upload directly to the vehicle via MAVLink mission protocol. "
        "Actions: add_takeoff (set takeoff altitude), add_waypoint (add a waypoint), "
        "remove_waypoint (remove by index), clear (remove all), list (show pending waypoints), "
        "add_rtl (append return-to-launch), upload (send to vehicle), download (load mission from vehicle). "
        "Typical flow: clear -> add_waypoint (xN) -> add_rtl -> upload. "
        "Waypoints are automatically cleared after a successful upload. "
        "Switch to AUTO mode after upload to fly the mission."
    );
}

QJsonObject MissionTool::parametersSchema() const
{
    QJsonObject props;

    QJsonObject action;
    action[QStringLiteral("type")] = QStringLiteral("string");
    action[QStringLiteral("description")] = QStringLiteral("The mission action to execute");
    QJsonArray actionEnum = {
        QStringLiteral("add_takeoff"), QStringLiteral("add_waypoint"),
        QStringLiteral("remove_waypoint"), QStringLiteral("add_rtl"),
        QStringLiteral("clear"), QStringLiteral("list"),
        QStringLiteral("upload"), QStringLiteral("download")
    };
    action[QStringLiteral("enum")] = actionEnum;
    props[QStringLiteral("action")] = action;

    // --- Common parameters ---
    QJsonObject latitude;
    latitude[QStringLiteral("type")] = QStringLiteral("number");
    latitude[QStringLiteral("description")] = QStringLiteral("Latitude");
    props[QStringLiteral("latitude")] = latitude;

    QJsonObject longitude;
    longitude[QStringLiteral("type")] = QStringLiteral("number");
    longitude[QStringLiteral("description")] = QStringLiteral("Longitude");
    props[QStringLiteral("longitude")] = longitude;

    QJsonObject altitude;
    altitude[QStringLiteral("type")] = QStringLiteral("number");
    altitude[QStringLiteral("description")] = QStringLiteral(
        "Altitude in meters above home. Default: 50 for waypoints, 10 for takeoff.");
    props[QStringLiteral("altitude")] = altitude;

    // --- Waypoint-specific parameters ---
    QJsonObject yaw;
    yaw[QStringLiteral("type")] = QStringLiteral("number");
    yaw[QStringLiteral("description")] = QStringLiteral(
        "Desired yaw angle in degrees at this waypoint. Omit for vehicle default (e.g. yaw towards next WP).");
    props[QStringLiteral("yaw")] = yaw;

    QJsonObject acceptance;
    acceptance[QStringLiteral("type")] = QStringLiteral("number");
    acceptance[QStringLiteral("description")] = QStringLiteral(
        "Acceptance radius in meters. 0 = vehicle default.");
    props[QStringLiteral("acceptance_radius")] = acceptance;

    QJsonObject holdTime;
    holdTime[QStringLiteral("type")] = QStringLiteral("number");
    holdTime[QStringLiteral("description")] = QStringLiteral(
        "Hold time in seconds at this waypoint (rotary wing). 0 = fly through.");
    props[QStringLiteral("hold_time")] = holdTime;

    // --- remove_waypoint parameter ---
    QJsonObject index;
    index[QStringLiteral("type")] = QStringLiteral("integer");
    index[QStringLiteral("description")] = QStringLiteral("Zero-based index for remove_waypoint");
    props[QStringLiteral("index")] = index;

    QJsonArray required;
    required.append(QStringLiteral("action"));

    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = props;
    schema[QStringLiteral("required")] = required;
    return schema;
}

static double safeDouble(const QJsonObject &p, const QString &key, double def)
{
    if (!p.contains(key)) return def;
    double v = p[key].toDouble(def);
    return qIsNaN(v) ? def : v;
}

QJsonObject MissionTool::_waypointToJson(int index, const Waypoint &wp) const
{
    QJsonObject wpObj;
    wpObj[QStringLiteral("index")] = index;
    wpObj[QStringLiteral("latitude")] = wp.lat;
    wpObj[QStringLiteral("longitude")] = wp.lon;
    wpObj[QStringLiteral("altitude")] = wp.alt;
    if (!qIsNaN(wp.yaw)) wpObj[QStringLiteral("yaw")] = wp.yaw;
    if (wp.acceptance > 0) wpObj[QStringLiteral("acceptance_radius")] = wp.acceptance;
    if (wp.holdTime > 0) wpObj[QStringLiteral("hold_time")] = wp.holdTime;
    return wpObj;
}

QJsonObject MissionTool::execute(const QJsonObject &parameters)
{
    QJsonObject result;
    QString action = parameters[QStringLiteral("action")].toString();

    // ===== Local operations (no vehicle needed) =====

    if (action == QStringLiteral("add_takeoff")) {
        double alt = safeDouble(parameters, QStringLiteral("altitude"), 10);
        _takeoffAlt = alt;
        _hasTakeoff = true;
        result[QStringLiteral("result")] = QStringLiteral("Takeoff altitude set to %1m. %2 waypoints pending.")
            .arg(alt).arg(_waypoints.size());
        result[QStringLiteral("status")] = QStringLiteral("success");
        return result;
    }

    if (action == QStringLiteral("add_waypoint")) {
        double lat = safeDouble(parameters, QStringLiteral("latitude"), 0);
        double lon = safeDouble(parameters, QStringLiteral("longitude"), 0);
        double alt = safeDouble(parameters, QStringLiteral("altitude"), 50);

        // Coordinate validation
        if (qFuzzyIsNull(lat) && qFuzzyIsNull(lon)) {
            result[QStringLiteral("error")] = QStringLiteral(
                "Coordinates (0, 0) are in the Gulf of Guinea — likely a placeholder. "
                "Provide the actual target latitude/longitude.");
            result[QStringLiteral("status")] = QStringLiteral("error");
            return result;
        }
        QGeoCoordinate coord(lat, lon, alt);
        if (!coord.isValid()) {
            result[QStringLiteral("error")] = QStringLiteral("Invalid coordinates: lat=%1, lon=%2").arg(lat).arg(lon);
            result[QStringLiteral("status")] = QStringLiteral("error");
            return result;
        }

        // Warn if waypoint is far from vehicle
        Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
        if (v && v->coordinate().isValid()) {
            double distM = v->coordinate().distanceTo(coord);
            if (distM > 100000) {  // > 100 km
                result[QStringLiteral("error")] = QStringLiteral(
                    "Waypoint is %1 km from vehicle — likely incorrect coordinates. "
                    "Max allowed: 100 km.").arg(distM / 1000.0, 0, 'f', 1);
                result[QStringLiteral("status")] = QStringLiteral("error");
                return result;
            }
            if (distM > 10000) {
                result[QStringLiteral("warning")] = QStringLiteral(
                    "Waypoint is %1 km from vehicle — verify this is intended.").arg(distM / 1000.0, 0, 'f', 1);
            }
        }

        double yaw = safeDouble(parameters, QStringLiteral("yaw"),
                                std::numeric_limits<double>::quiet_NaN());
        double acceptance = safeDouble(parameters, QStringLiteral("acceptance_radius"), 0);
        double holdTime = safeDouble(parameters, QStringLiteral("hold_time"), 0);

        _waypoints.append({lat, lon, alt, yaw, acceptance, holdTime});

        QString desc = QStringLiteral("WP%1: (%2, %3) %4m  [%5 waypoints total]")
            .arg(_waypoints.size()).arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(alt).arg(_waypoints.size());
        if (!qIsNaN(yaw)) desc += QStringLiteral(" yaw=%1°").arg(yaw);
        if (acceptance > 0) desc += QStringLiteral(" acc=%1m").arg(acceptance);
        if (holdTime > 0) desc += QStringLiteral(" hold=%1s").arg(holdTime);
        result[QStringLiteral("result")] = desc;
        result[QStringLiteral("status")] = QStringLiteral("success");
        return result;
    }

    if (action == QStringLiteral("add_rtl")) {
        _waypoints.append({0, 0, 0,
                           std::numeric_limits<double>::quiet_NaN(), 0, 0});
        // Mark as RTL — we'll handle this in _buildMissionItems
        result[QStringLiteral("result")] = QStringLiteral("RTL appended. %1 items total (takeoff + waypoints + RTL).")
            .arg((_hasTakeoff ? 1 : 0) + _waypoints.size());
        result[QStringLiteral("status")] = QStringLiteral("success");
        return result;
    }

    if (action == QStringLiteral("remove_waypoint")) {
        int idx = parameters[QStringLiteral("index")].toInt(-1);
        if (idx < 0 || idx >= _waypoints.size()) {
            result[QStringLiteral("error")] = QStringLiteral("Invalid index: %1 (0-%2)")
                .arg(idx).arg(_waypoints.size() - 1);
            result[QStringLiteral("status")] = QStringLiteral("error");
            return result;
        }
        auto wp = _waypoints.takeAt(idx);
        result[QStringLiteral("result")] = QStringLiteral("Removed WP%1: (%2, %3). %4 waypoints remaining.")
            .arg(idx).arg(wp.lat, 0, 'f', 6).arg(wp.lon, 0, 'f', 6).arg(_waypoints.size());
        result[QStringLiteral("status")] = QStringLiteral("success");
        return result;
    }

    if (action == QStringLiteral("clear")) {
        int count = _waypoints.size();
        _waypoints.clear();
        _hasTakeoff = false;
        _takeoffAlt = 50;
        result[QStringLiteral("result")] = QStringLiteral("Cleared %1 waypoints. Mission buffer is now empty.").arg(count);
        result[QStringLiteral("status")] = QStringLiteral("success");
        return result;
    }

    if (action == QStringLiteral("list")) {
        QJsonArray arr;
        for (int i = 0; i < _waypoints.size(); i++) {
            arr.append(_waypointToJson(i, _waypoints[i]));
        }
        result[QStringLiteral("waypoints")] = arr;
        result[QStringLiteral("count")] = _waypoints.size();
        result[QStringLiteral("takeoff_alt")] = _takeoffAlt;
        result[QStringLiteral("has_takeoff")] = _hasTakeoff;
        result[QStringLiteral("result")] = _waypoints.isEmpty()
            ? QStringLiteral("No pending waypoints. Use add_waypoint to build a mission.")
            : QStringLiteral("%1 waypoints pending upload (takeoff: %2)")
                .arg(_waypoints.size()).arg(_hasTakeoff ? QStringLiteral("yes, %1m").arg(_takeoffAlt) : QStringLiteral("no"));
        result[QStringLiteral("status")] = QStringLiteral("success");
        return result;
    }

    // ===== Vehicle-dependent operations =====

    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        result[QStringLiteral("error")] = QStringLiteral("No vehicle connected");
        result[QStringLiteral("status")] = QStringLiteral("error");
        return result;
    }

    if (action == QStringLiteral("upload")) {
        if (_waypoints.isEmpty()) {
            result[QStringLiteral("error")] = QStringLiteral("No waypoints to upload. Use add_waypoint first.");
            result[QStringLiteral("status")] = QStringLiteral("error");
            return result;
        }

        // Pre-check: vehicle position for home coordinate
        QGeoCoordinate vehiclePos = v->coordinate();
        if (!vehiclePos.isValid() || (qFuzzyIsNull(vehiclePos.latitude()) && qFuzzyIsNull(vehiclePos.longitude()))) {
            result[QStringLiteral("error")] = QStringLiteral(
                "Vehicle position is invalid (no GPS fix). Cannot set mission home position. "
                "Wait for GPS lock before uploading.");
            result[QStringLiteral("status")] = QStringLiteral("error");
            return result;
        }

        // Pre-check: armed + mission mode (same as PlanView's sendToVehiclePreCheck)
        if (v->armed() && v->flightMode() == v->missionFlightMode()) {
            result[QStringLiteral("error")] = QStringLiteral(
                "Vehicle is armed and in mission mode — cannot replace active mission. "
                "Pause the mission or switch flight mode first.");
            result[QStringLiteral("status")] = QStringLiteral("error");
            return result;
        }

        // Check that MissionManager is not already busy
        auto *mm = v->missionManager();
        if (mm->inProgress()) {
            result[QStringLiteral("error")] = QStringLiteral(
                "Mission manager is busy (another upload/download in progress). Wait and retry.");
            result[QStringLiteral("status")] = QStringLiteral("error");
            return result;
        }

        // Build MissionItem list directly — no .plan file, no PlanMasterController
        QList<MissionItem *> items;
        int seq = 0;

        // Item 0: Home position (required by protocol, vehicle fills actual values)
        items.append(new MissionItem(seq++,
            MAV_CMD_NAV_WAYPOINT, MAV_FRAME_GLOBAL,
            0, 0, 0, 0,                             // param1-4
            vehiclePos.latitude(), vehiclePos.longitude(), vehiclePos.altitude(),
            true, false));

        // Takeoff item
        if (_hasTakeoff) {
            items.append(new MissionItem(seq++,
                MAV_CMD_NAV_TAKEOFF, MAV_FRAME_GLOBAL_RELATIVE_ALT,
                0, 0, 0,                             // param1-3: min_pitch, empty, empty
                std::numeric_limits<double>::quiet_NaN(),  // param4: yaw (NaN = vehicle default)
                std::numeric_limits<double>::quiet_NaN(),  // param5: lat (NaN = current)
                std::numeric_limits<double>::quiet_NaN(),  // param6: lon (NaN = current)
                _takeoffAlt,                          // param7: takeoff altitude
                true, false));
        }

        // Waypoint items
        for (const auto &wp : _waypoints) {
            // RTL waypoint: lat=0, lon=0, alt=0 → MAV_CMD_NAV_RETURN_TO_LAUNCH
            if (qFuzzyIsNull(wp.lat) && qFuzzyIsNull(wp.lon) && qFuzzyIsNull(wp.alt)) {
                items.append(new MissionItem(seq++,
                    MAV_CMD_NAV_RETURN_TO_LAUNCH, MAV_FRAME_GLOBAL_RELATIVE_ALT,
                    0, 0, 0, 0,
                    0, 0, 0,
                    true, false));
                continue;
            }

            items.append(new MissionItem(seq++,
                MAV_CMD_NAV_WAYPOINT, MAV_FRAME_GLOBAL_RELATIVE_ALT,
                wp.holdTime,                          // param1: hold time
                wp.acceptance,                        // param2: acceptance radius
                0,                                    // param3: pass radius
                qIsNaN(wp.yaw) ? std::numeric_limits<double>::quiet_NaN() : wp.yaw,  // param4: yaw
                wp.lat,                               // param5: latitude
                wp.lon,                               // param6: longitude
                wp.alt,                               // param7: altitude
                true, false));
        }

        // Upload via MissionManager — same path as QGC's Plan View
        // PlanManager takes ownership of MissionItem pointers
        int wpCount = _waypoints.size();
        mm->writeMissionItems(items);

        // Async: wait for sendComplete signal
        // We use a local event loop here because execute() must return a result.
        // The alternative (making execute() async) would require refactoring AgentTool.
        constexpr int kUploadTimeoutMs = 15000;
        bool uploadSuccess = false;
        bool uploadError = false;
        QString uploadErrorMsg;

        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);

        auto completeConn = QObject::connect(
            mm, &MissionManager::sendComplete, &loop,
            [&uploadSuccess, &uploadError, &uploadErrorMsg, &loop](bool error) {
                if (error) {
                    uploadError = true;
                    uploadErrorMsg = QStringLiteral("Flight controller rejected mission upload");
                } else {
                    uploadSuccess = true;
                }
                loop.quit();
            });
        auto timeoutConn = QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

        timeout.start(kUploadTimeoutMs);
        loop.exec();

        QObject::disconnect(completeConn);
        QObject::disconnect(timeoutConn);

        if (uploadSuccess) {
            _waypoints.clear();
            _hasTakeoff = false;
            _takeoffAlt = 50;
            result[QStringLiteral("result")] = QStringLiteral(
                "Successfully uploaded %1 waypoints (FC confirmed). "
                "Local buffer cleared. Switch to AUTO mode to fly the mission.").arg(wpCount);
            result[QStringLiteral("waypoints_uploaded")] = wpCount;
            result[QStringLiteral("status")] = QStringLiteral("success");
        } else if (uploadError) {
            result[QStringLiteral("error")] = uploadErrorMsg;
            result[QStringLiteral("status")] = QStringLiteral("error");
        } else {
            result[QStringLiteral("result")] = QStringLiteral(
                "Upload of %1 waypoints timed out — FC did not confirm within %2s. "
                "Mission may have partially uploaded. Check vehicle status.").arg(wpCount).arg(kUploadTimeoutMs / 1000);
            result[QStringLiteral("status")] = QStringLiteral("error");
        }
        return result;
    }

    if (action == QStringLiteral("download")) {
        auto *mm = v->missionManager();

        if (mm->inProgress()) {
            result[QStringLiteral("error")] = QStringLiteral(
                "Mission manager is busy (another upload/download in progress). Wait and retry.");
            result[QStringLiteral("status")] = QStringLiteral("error");
            return result;
        }

        mm->loadFromVehicle();

        constexpr int kDownloadTimeoutMs = 10000;
        bool downloadSuccess = false;
        bool downloadError = false;
        QString downloadErrorMsg;

        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);

        auto availableConn = QObject::connect(
            mm, &MissionManager::newMissionItemsAvailable, &loop,
            [&downloadSuccess, &loop](bool /*removeAllRequested*/) {
                downloadSuccess = true;
                loop.quit();
            });
        auto errorConn = QObject::connect(
            mm, &MissionManager::error, &loop,
            [&downloadError, &downloadErrorMsg, &loop](int errorCode, const QString &errorMsg) {
                Q_UNUSED(errorCode);
                downloadError = true;
                downloadErrorMsg = errorMsg;
                loop.quit();
            });
        auto timeoutConn = QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

        timeout.start(kDownloadTimeoutMs);
        loop.exec();

        QObject::disconnect(availableConn);
        QObject::disconnect(errorConn);
        QObject::disconnect(timeoutConn);

        if (downloadSuccess) {
            const QList<MissionItem *> &items = mm->missionItems();
            QJsonArray wpArray;
            for (const auto *item : items) {
                QJsonObject itemObj;
                itemObj[QStringLiteral("seq")] = item->sequenceNumber();
                itemObj[QStringLiteral("command")] = static_cast<int>(item->command());

                QGeoCoordinate coord = item->coordinate();
                if (coord.isValid()) {
                    itemObj[QStringLiteral("latitude")] = coord.latitude();
                    itemObj[QStringLiteral("longitude")] = coord.longitude();
                    itemObj[QStringLiteral("altitude")] = coord.altitude();
                }

                QJsonArray params;
                params.append(item->param1());
                params.append(item->param2());
                params.append(item->param3());
                params.append(item->param4());
                params.append(item->param5());
                params.append(item->param6());
                params.append(item->param7());
                itemObj[QStringLiteral("params")] = params;

                int cmd = static_cast<int>(item->command());
                switch (cmd) {
                case 16: itemObj[QStringLiteral("type")] = QStringLiteral("WAYPOINT"); break;
                case 17: itemObj[QStringLiteral("type")] = QStringLiteral("LOITER_UNLIM"); break;
                case 18: itemObj[QStringLiteral("type")] = QStringLiteral("LOITER_TURNS"); break;
                case 19: itemObj[QStringLiteral("type")] = QStringLiteral("LOITER_TIME"); break;
                case 20: itemObj[QStringLiteral("type")] = QStringLiteral("RETURN_TO_LAUNCH"); break;
                case 21: itemObj[QStringLiteral("type")] = QStringLiteral("LAND"); break;
                case 22: itemObj[QStringLiteral("type")] = QStringLiteral("TAKEOFF"); break;
                default: itemObj[QStringLiteral("type")] = QStringLiteral("CMD_%1").arg(cmd); break;
                }

                wpArray.append(itemObj);
            }

            result[QStringLiteral("mission_items")] = wpArray;
            result[QStringLiteral("count")] = items.size();
            result[QStringLiteral("result")] = QStringLiteral("Downloaded %1 mission items from vehicle").arg(items.size());
            result[QStringLiteral("status")] = QStringLiteral("success");
        } else if (downloadError) {
            result[QStringLiteral("error")] = QStringLiteral("Download failed: %1").arg(downloadErrorMsg);
            result[QStringLiteral("status")] = QStringLiteral("error");
        } else {
            result[QStringLiteral("error")] = QStringLiteral("Download timed out — FC did not respond within %1s").arg(kDownloadTimeoutMs / 1000);
            result[QStringLiteral("status")] = QStringLiteral("error");
        }
        return result;
    }

    result[QStringLiteral("error")] = QStringLiteral("Unknown action: %1").arg(action);
    result[QStringLiteral("status")] = QStringLiteral("error");
    return result;
}
