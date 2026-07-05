#pragma once

#include "../AgentTool.h"

class ParameterTool : public AgentTool
{
    Q_OBJECT
public:
    enum class Mode { Get, Set, List };

    explicit ParameterTool(Mode mode, QObject *parent = nullptr);

    QString name() const override;
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override;
    bool requiresConfirmation() const override;

private:
    Mode _mode;
};
