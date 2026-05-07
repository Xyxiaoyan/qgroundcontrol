#include "CommandEngine.h"
#include "Vehicle.h"
#include "Fact.h"
#include "ParameterManager.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QJsonObject>
#include <QtPositioning/QGeoCoordinate>

QGC_LOGGING_CATEGORY(CommandLog, "qgc.agent.cmd")

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
        QStringLiteral("param"), QStringLiteral("paramq"), QStringLiteral("paramlist")
    };

    while (it.hasNext()) {
        auto match = it.next();
        QString raw = match.captured(1).trimmed();
        QStringList parts = raw.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;

        QString action = parts.takeFirst().toLower();
        if (!validActions.contains(action)) continue;

        QList<double> params;
        for (const auto &p : parts) {
            bool ok;
            double v = p.toDouble(&ok);
            params.append(ok ? v : 0);
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
        {"param",      QStringLiteral("⚠ 修改参数")},
        {"paramq",     QStringLiteral("查询参数")},
        {"paramlist",  QStringLiteral("列举参数")},
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
            QStringList names = pm->parameterNames(-1);
            QStringList out;
            for (const auto &n : names) {
                if (n.toUpper().contains(pat) || pat.isEmpty()) {
                    auto *f = pm->getParameter(-1, n);
                    out.append(QStringLiteral("%1=%2").arg(n).arg(f ? f->rawValue().toString() : QStringLiteral("?")));
                    if (out.size() >= 30) break;
                }
            }
            emit commandExecuted(out.isEmpty() ? QStringLiteral("无匹配参数") : out.join(QStringLiteral("\n")));
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
