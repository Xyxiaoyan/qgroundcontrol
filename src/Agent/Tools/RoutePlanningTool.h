#pragma once

#include "../AgentTool.h"

class ContextProvider;

class RoutePlanningTool : public AgentTool
{
    Q_OBJECT
public:
    explicit RoutePlanningTool(ContextProvider *ctx, QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("plan_driving_route"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override { return true; }

private:
    ContextProvider *_ctx = nullptr;
};
