#include "RelativeMoveTool.h"
#include "Vehicle.h"
#include "MultiVehicleManager.h"

#include <QtPositioning/QGeoCoordinate>
#include <cmath>

RelativeMoveTool::RelativeMoveTool(QObject *parent)
    : AgentTool(parent)
{
}

QString RelativeMoveTool::description() const
{
    return QStringLiteral(
        "Move the vehicle relative to its current heading (body frame). "
        "Directions: forward (nose direction), back, left, right, or turn (rotate in place). "
        "Uses current GPS position and heading to compute target coordinates."
    );
}

QJsonObject RelativeMoveTool::parametersSchema() const
{
    QJsonObject props;

    QJsonObject direction;
    direction[QStringLiteral("type")] = QStringLiteral("string");
    direction[QStringLiteral("description")] = QStringLiteral("Movement direction relative to vehicle heading");
    QJsonArray dirEnum = {QStringLiteral("forward"), QStringLiteral("back"), QStringLiteral("left"),
                          QStringLiteral("right"), QStringLiteral("turn")};
    direction[QStringLiteral("enum")] = dirEnum;
    props[QStringLiteral("direction")] = direction;

    QJsonObject meters;
    meters[QStringLiteral("type")] = QStringLiteral("number");
    meters[QStringLiteral("description")] = QStringLiteral("Distance in meters (default 5)");
    props[QStringLiteral("meters")] = meters;

    QJsonObject degrees;
    degrees[QStringLiteral("type")] = QStringLiteral("number");
    degrees[QStringLiteral("description")] = QStringLiteral("Turn angle in degrees, positive=clockwise (only for turn, default 30)");
    props[QStringLiteral("degrees")] = degrees;

    QJsonArray required;
    required.append(QStringLiteral("direction"));

    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = props;
    schema[QStringLiteral("required")] = required;
    return schema;
}

static double safeParam2(const QJsonObject &p, const QString &key, double def)
{
    if (!p.contains(key)) return def;
    double v = p[key].toDouble(def);
    return qIsNaN(v) ? def : v;
}

QJsonObject RelativeMoveTool::execute(const QJsonObject &parameters)
{
    QJsonObject result;
    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        result[QStringLiteral("error")] = QStringLiteral("No vehicle connected");
        return result;
    }

    QString dir = parameters[QStringLiteral("direction")].toString();
    double meters = safeParam2(parameters, QStringLiteral("meters"), 5);
    double degrees = safeParam2(parameters, QStringLiteral("degrees"), 30);

    QGeoCoordinate pos = v->coordinate();
    double heading = 0;
    auto *hFact = v->heading();
    if (hFact) heading = hFact->rawValue().toDouble();

    if (dir == QStringLiteral("turn")) {
        heading += degrees;
        while (heading < 0) heading += 360;
        while (heading >= 360) heading -= 360;
        meters = 1.0; // Move 1m to trigger heading change
    } else {
        if (dir == QStringLiteral("back")) heading += 180;
        else if (dir == QStringLiteral("right")) heading -= 90;
        else if (dir == QStringLiteral("left")) heading += 90;
        while (heading < 0) heading += 360;
        while (heading >= 360) heading -= 360;
    }

    double radH = heading * M_PI / 180.0;
    double radLat = pos.latitude() * M_PI / 180.0;
    double dLat = meters * cos(radH) / 111320.0;
    double dLon = meters * sin(radH) / (111320.0 * cos(radLat));
    QGeoCoordinate target(pos.latitude() + dLat, pos.longitude() + dLon, pos.altitude());

    try {
        v->guidedModeGotoLocation(target);
        if (dir == QStringLiteral("turn")) {
            result[QStringLiteral("result")] = QStringLiteral("✓ Turn %1°").arg(degrees);
        } else {
            result[QStringLiteral("result")] = QStringLiteral("✓ %1 %2m").arg(dir).arg(meters);
        }
    } catch (const std::exception &e) {
        result[QStringLiteral("error")] = QStringLiteral("Exception: %1").arg(e.what());
    }

    result[QStringLiteral("status")] = result.contains(QStringLiteral("error")) ? QStringLiteral("error") : QStringLiteral("success");
    return result;
}
