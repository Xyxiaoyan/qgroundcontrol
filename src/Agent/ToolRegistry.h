#pragma once

#include <QtCore/QObject>
#include <QtCore/QHash>
#include <QtCore/QVector>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonDocument>

class AgentTool;

class ToolRegistry : public QObject
{
    Q_OBJECT
public:
    struct ParsedFunctionCall {
        QString name;
        QJsonArray arguments;      // One or more argument sets (split from concatenated JSON)
        QString rawArguments;
    };

    explicit ToolRegistry(QObject *parent = nullptr);

    void registerTool(AgentTool *tool);
    AgentTool *getTool(const QString &name) const;
    QVector<AgentTool *> allTools() const;

    QJsonArray generateOpenAITools() const;

    static ParsedFunctionCall parseFunctionCall(const QJsonObject &assistantMessage);

signals:
    void toolRegistered(const QString &name);

private:
    QHash<QString, AgentTool *> _tools;
};
