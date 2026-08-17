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
    const QJsonValue argumentsValue = fc.value(QStringLiteral("arguments"));
    if (argumentsValue.isObject()) {
        result.arguments.append(argumentsValue.toObject());
        result.rawArguments = QString::fromUtf8(QJsonDocument(argumentsValue.toObject()).toJson(QJsonDocument::Compact));
        return result;
    }
    if (argumentsValue.isArray()) {
        const QJsonArray array = argumentsValue.toArray();
        for (const QJsonValue &value : array) {
            if (value.isObject()) {
                result.arguments.append(value.toObject());
            }
        }
        result.rawArguments = QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
        return result;
    }
    result.rawArguments = argumentsValue.toString();
    if (!result.rawArguments.isEmpty()) {
        QString raw = result.rawArguments.trimmed();
        // Parse the standard single-object form first.
        QJsonArray argList;
        QJsonParseError parseError;
        const QJsonDocument singleDocument = QJsonDocument::fromJson(raw.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && singleDocument.isObject()) {
            argList.append(singleDocument.object());
        } else if (parseError.error == QJsonParseError::NoError && singleDocument.isArray()) {
            for (const QJsonValue &value : singleDocument.array()) {
                if (value.isObject()) {
                    argList.append(value.toObject());
                }
            }
        }

        // Compatibility for models that concatenate objects instead of issuing
        // separate tool calls: {"k":"v"}{"k2":"v2"}.
        int depth = 0;
        int start = -1;
        bool inString = false;
        bool escaped = false;
        for (int i = 0; argList.isEmpty() && i < raw.size(); i++) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (raw[i] == '\\' && inString) {
                escaped = true;
                continue;
            }
            if (raw[i] == '"') {
                inString = !inString;
                continue;
            }
            if (inString) continue;
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
        if (argList.size() > 1) {
            qWarning() << "Split concatenated JSON into" << argList.size() << "argument sets";
        }
        result.arguments = argList;
    }
    // An empty string is accepted for providers that omit `{}` on no-argument
    // tools. Schema validation in ReActLoop rejects it for tools with required fields.
    if (result.arguments.isEmpty()) {
        result.arguments.append(QJsonObject());
        qWarning() << "parseFunctionCall: arguments empty/unparseable for" << result.name
                    << "— rawArguments:" << result.rawArguments;
    }
    return result;
}
