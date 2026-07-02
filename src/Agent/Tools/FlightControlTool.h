#pragma once

#include "../AgentTool.h"

class FlightControlTool : public AgentTool
{
    Q_OBJECT
public:
    explicit FlightControlTool(QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("flight_control"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override { return false; }
    QString preExecutionCheck(const QJsonObject &parameters, Vehicle *vehicle) const override;
};
