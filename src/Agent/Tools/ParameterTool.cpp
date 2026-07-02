#include "ParameterTool.h"
#include "Vehicle.h"
#include "Fact.h"
#include "FactMetaData.h"
#include "ParameterManager.h"
#include "MultiVehicleManager.h"

#include <cmath>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>

ParameterTool::ParameterTool(Mode mode, QObject *parent)
    : AgentTool(parent)
    , _mode(mode)
{
}

QString ParameterTool::name() const
{
    switch (_mode) {
    case Mode::Get:  return QStringLiteral("get_parameter");
    case Mode::Set:  return QStringLiteral("set_parameter");
    case Mode::List: return QStringLiteral("list_parameters");
    }
    return QStringLiteral("parameter");
}

QString ParameterTool::description() const
{
    switch (_mode) {
    case Mode::Get:
        return QStringLiteral(
            "Read a single vehicle parameter value by name. "
            "Use to query current parameter settings (e.g. MC_ROLL_P, BAT_N_CELLS, COM_DISARM_LAND)."
        );
    case Mode::Set:
        return QStringLiteral(
            "Modify a vehicle parameter. Writes to flight controller — use with caution. "
            "Setting incorrect values can cause unstable flight."
        );
    case Mode::List:
        return QStringLiteral(
            "Search vehicle parameters by name substring (case-insensitive). "
            "Use the parameter prefix directly (e.g. COM_, BAT_, EKF2_, MC_, NAV_, MIS_, SYS_, RC_, GPS_, RATE_, FW_, VT_). "
            "Returns up to 50 matching name=value pairs."
        );
    }
    return {};
}

QJsonObject ParameterTool::parametersSchema() const
{
    QJsonObject props;
    QJsonArray required;

    switch (_mode) {
    case Mode::Get: {
        QJsonObject name;
        name[QStringLiteral("type")] = QStringLiteral("string");
        name[QStringLiteral("description")] = QStringLiteral("Parameter name to read (e.g. MC_ROLL_P, BAT_N_CELLS)");
        props[QStringLiteral("name")] = name;
        required.append(QStringLiteral("name"));
        break;
    }
    case Mode::Set: {
        QJsonObject name;
        name[QStringLiteral("type")] = QStringLiteral("string");
        name[QStringLiteral("description")] = QStringLiteral("Parameter name to modify (e.g. MC_ROLL_P)");
        props[QStringLiteral("name")] = name;

        QJsonObject value;
        value[QStringLiteral("type")] = QStringLiteral("number");
        value[QStringLiteral("description")] = QStringLiteral("New value to write");
        props[QStringLiteral("value")] = value;

        required.append(QStringLiteral("name"));
        required.append(QStringLiteral("value"));
        break;
    }
    case Mode::List: {
        QJsonObject keyword;
        keyword[QStringLiteral("type")] = QStringLiteral("string");
        keyword[QStringLiteral("description")] = QStringLiteral("Parameter name substring to search (e.g. COM_, BAT_, EKF2_, MC_)");
        props[QStringLiteral("keyword")] = keyword;
        required.append(QStringLiteral("keyword"));
        break;
    }
    }

    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = props;
    schema[QStringLiteral("required")] = required;
    return schema;
}

bool ParameterTool::isReadOnly() const
{
    return _mode != Mode::Set;
}

bool ParameterTool::requiresConfirmation() const
{
    return _mode == Mode::Set;
}

QJsonObject ParameterTool::execute(const QJsonObject &parameters)
{
    QJsonObject result;
    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();
    if (!v) {
        result[QStringLiteral("error")] = QStringLiteral("No vehicle connected");
        return result;
    }

    auto *pm = v->parameterManager();
    if (!pm) {
        result[QStringLiteral("error")] = QStringLiteral("Parameter system not available");
        return result;
    }

    // Check that parameters are fully loaded from vehicle
    if (!pm->parametersReady()) {
        result[QStringLiteral("error")] = QStringLiteral("Parameters not ready — still loading from vehicle (%1%)").arg(static_cast<int>(pm->loadProgress() * 100));
        return result;
    }

    if (_mode == Mode::Get) {
        QString name = parameters[QStringLiteral("name")].toString();
        if (name.isEmpty()) {
            result[QStringLiteral("error")] = QStringLiteral("Parameter name is required");
            return result;
        }
        if (!pm->parameterExists(-1, name)) {
            result[QStringLiteral("error")] = QStringLiteral("Parameter %1 not found").arg(name);
            return result;
        }
        auto *f = pm->getParameter(-1, name);
        result[QStringLiteral("result")] = QStringLiteral("%1 = %2").arg(name, f->rawValue().toString());

    } else if (_mode == Mode::Set) {
        QString name = parameters[QStringLiteral("name")].toString();
        if (name.isEmpty()) {
            result[QStringLiteral("error")] = QStringLiteral("Parameter name is required");
            return result;
        }
        if (!pm->parameterExists(-1, name)) {
            result[QStringLiteral("error")] = QStringLiteral("Parameter %1 not found").arg(name);
            return result;
        }
        auto *f = pm->getParameter(-1, name);

        // Reject read-only parameters
        if (f->readOnly()) {
            result[QStringLiteral("error")] = QStringLiteral("Parameter %1 is read-only").arg(name);
            return result;
        }

        // Reject volatile parameters (continuously updated by firmware)
        if (f->volatileValue()) {
            result[QStringLiteral("error")] = QStringLiteral("Parameter %1 is volatile — writing has no lasting effect").arg(name);
            return result;
        }

        double val = parameters[QStringLiteral("value")].toDouble();
        if (qIsNaN(val)) {
            result[QStringLiteral("error")] = QStringLiteral("Invalid value: not a number");
            return result;
        }

        // Validate against Fact min/max metadata
        QVariant rawMin = f->rawMin();
        QVariant rawMax = f->rawMax();
        if (rawMin.isValid() && val < rawMin.toDouble()) {
            result[QStringLiteral("error")] = QStringLiteral("Value %1 below minimum %2 for parameter %3").arg(val).arg(rawMin.toDouble()).arg(name);
            return result;
        }
        if (rawMax.isValid() && val > rawMax.toDouble()) {
            result[QStringLiteral("error")] = QStringLiteral("Value %1 above maximum %2 for parameter %3").arg(val).arg(rawMax.toDouble()).arg(name);
            return result;
        }

        // Write value — Fact::setRawValue triggers async MAVLink PARAM_SET.
        // Wait for flight controller ACK via ParameterManager signals.
        f->setRawValue(val);

        // Wait up to 3s for FC confirmation (covers 2 retries × 1s ACK timeout)
        constexpr int kAckTimeoutMs = 3000;
        bool writeConfirmed = false;
        bool writeFailed = false;

        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);

        // Match on parameter name; componentId is resolved internally by ParameterManager
        auto successConn = QObject::connect(
            pm, &ParameterManager::_paramSetSuccess, &loop,
            [&writeConfirmed, &loop, &name](int /*componentId*/, const QString &paramName) {
                if (paramName == name) {
                    writeConfirmed = true;
                    loop.quit();
                }
            });
        auto failureConn = QObject::connect(
            pm, &ParameterManager::_paramSetFailure, &loop,
            [&writeFailed, &loop, &name](int /*componentId*/, const QString &paramName) {
                if (paramName == name) {
                    writeFailed = true;
                    loop.quit();
                }
            });
        auto timeoutConn = QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

        timeout.start(kAckTimeoutMs);
        loop.exec();

        // Cleanup connections
        QObject::disconnect(successConn);
        QObject::disconnect(failureConn);
        QObject::disconnect(timeoutConn);

        // Report result based on FC response
        QString actualVal = f->rawValue().toString();
        if (writeConfirmed) {
            result[QStringLiteral("result")] = QStringLiteral("%1 = %2 (FC confirmed)").arg(name, actualVal);
            result[QStringLiteral("value_written")] = actualVal;
        } else if (writeFailed) {
            result[QStringLiteral("error")] = QStringLiteral("Flight controller rejected parameter %1 — value reverted to %2").arg(name, actualVal);
        } else {
            // Timeout — FC did not respond within kAckTimeoutMs
            result[QStringLiteral("result")] = QStringLiteral("%1 = %2 (FC ack timeout — write may have failed)").arg(name, actualVal);
            result[QStringLiteral("value_written")] = actualVal;
        }

    } else if (_mode == Mode::List) {
        QString keyword = parameters[QStringLiteral("keyword")].toString().trimmed();
        if (keyword.isEmpty()) {
            result[QStringLiteral("error")] = QStringLiteral("Keyword is required. Examples: COM_, BAT_, EKF2_, MC_, NAV_, RATE_, GPS_, FW_");
            return result;
        }

        QStringList names = pm->parameterNames(-1);
        QStringList out;
        QString kwUpper = keyword.toUpper();
        for (const auto &n : names) {
            if (n.toUpper().contains(kwUpper)) {
                auto *f = pm->getParameter(-1, n);
                out.append(QStringLiteral("%1=%2").arg(n).arg(f ? f->rawValue().toString() : QStringLiteral("?")));
                if (out.size() >= 50) break;
            }
        }
        result[QStringLiteral("result")] = out.isEmpty()
            ? QStringLiteral("No parameters matching '%1'").arg(keyword)
            : out.join(QStringLiteral("\n"));
    }

    result[QStringLiteral("status")] = result.contains(QStringLiteral("error")) ? QStringLiteral("error") : QStringLiteral("success");
    return result;
}
