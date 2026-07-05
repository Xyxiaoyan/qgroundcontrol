#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>

class Vehicle;

class AgentTool : public QObject
{
    Q_OBJECT
public:
    explicit AgentTool(QObject *parent = nullptr) : QObject(parent) {}
    ~AgentTool() override = default;

    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QJsonObject parametersSchema() const = 0;
    virtual QJsonObject execute(const QJsonObject &parameters) = 0;

    virtual bool requiresConfirmation() const { return !isReadOnly(); }
    virtual bool isReadOnly() const { return false; }

    // Optional pre-execution check (returns empty QString on success, error message on failure)
    virtual QString preExecutionCheck(const QJsonObject &parameters, Vehicle *vehicle) const
    {
        Q_UNUSED(parameters)
        Q_UNUSED(vehicle)
        return {};
    }

    QJsonObject toOpenAITool() const
    {
        QJsonObject func;
        func[QStringLiteral("name")] = name();
        func[QStringLiteral("description")] = description();
        func[QStringLiteral("parameters")] = parametersSchema();

        QJsonObject tool;
        tool[QStringLiteral("type")] = QStringLiteral("function");
        tool[QStringLiteral("function")] = func;
        return tool;
    }
};
