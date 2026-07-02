#include "ToolRegistry.h"
#include "AgentTool.h"

ToolRegistry::ToolRegistry(QObject *parent)
    : QObject(parent)
{
}

void ToolRegistry::registerTool(AgentTool *tool)
{
    if (!tool) return;
    _tools.insert(tool->name(), tool);
    emit toolRegistered(tool->name());
}

AgentTool *ToolRegistry::getTool(const QString &name) const
{
    return _tools.value(name, nullptr);
}

QVector<AgentTool *> ToolRegistry::allTools() const
{
    QVector<AgentTool *> result;
    result.reserve(_tools.size());
    for (auto *tool : _tools) {
        result.append(tool);
    }
    return result;
}

QJsonArray ToolRegistry::generateOpenAITools() const
{
    QJsonArray arr;
    for (auto *tool : _tools) {
        arr.append(tool->toOpenAITool());
    }
    return arr;
}

ToolRegistry::ParsedFunctionCall ToolRegistry::parseFunctionCall(const QJsonObject &msgOrCall)
{
    ParsedFunctionCall result;
    QJsonObject fc;

    // If the object has a "name" key directly, it's already a function call object
    if (msgOrCall.contains(QStringLiteral("name"))) {
        fc = msgOrCall;
    } else {
        // Extract from assistant message: check function_call (legacy) or tool_calls (modern)
        fc = msgOrCall.value(QStringLiteral("function_call")).toObject();
        if (fc.isEmpty()) {
            QJsonArray toolCalls = msgOrCall.value(QStringLiteral("tool_calls")).toArray();
            if (!toolCalls.isEmpty()) {
                fc = toolCalls.first().toObject().value(QStringLiteral("function")).toObject();
            }
        }
    }
    if (fc.isEmpty()) return result;

    result.name = fc.value(QStringLiteral("name")).toString();
    result.rawArguments = fc.value(QStringLiteral("arguments")).toString();
    if (!result.rawArguments.isEmpty()) {
        QString raw = result.rawArguments.trimmed();
        // Split concatenated JSON objects: {"k":"v"}{"k2":"v2"}... → [{"k":"v"}, {"k2":"v2"}]
        QJsonArray argList;
        int depth = 0;
        int start = -1;
        for (int i = 0; i < raw.size(); i++) {
            if (raw[i] == '{') {
                if (depth == 0) start = i;
                depth++;
            } else if (raw[i] == '}') {
                depth--;
                if (depth == 0 && start >= 0) {
                    QString objStr = raw.mid(start, i - start + 1);
                    QJsonDocument doc = QJsonDocument::fromJson(objStr.toUtf8());
                    if (doc.isObject()) {
                        argList.append(doc.object());
                    }
                    start = -1;
                }
            }
        }
        if (argList.isEmpty()) {
            // Try parsing as single object
            QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
            if (doc.isObject()) {
                argList.append(doc.object());
            }
        }
        if (argList.size() > 1) {
            qWarning() << "Split concatenated JSON into" << argList.size() << "argument sets";
        }
        result.arguments = argList;
    }
    // If arguments string was empty or unparseable, default to a single empty object
    // so the tool still receives a valid (empty) QJsonObject rather than nothing.
    if (result.arguments.isEmpty()) {
        result.arguments.append(QJsonObject());
        qWarning() << "parseFunctionCall: arguments empty/unparseable for" << result.name
                    << "— rawArguments:" << result.rawArguments;
    }
    return result;
}
