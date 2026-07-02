#pragma once

#include "../AgentTool.h"

class ContextProvider;

class VehicleContextTool : public AgentTool
{
    Q_OBJECT
public:
    explicit VehicleContextTool(ContextProvider *ctx, QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("get_vehicle_state"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override { return true; }

private:
    ContextProvider *_ctx = nullptr;
};
