#pragma once

#include "../AgentTool.h"

class FlightModeTool : public AgentTool
{
    Q_OBJECT
public:
    explicit FlightModeTool(QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("flight_mode"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override { return false; }
    bool requiresConfirmation() const override { return true; }
};
