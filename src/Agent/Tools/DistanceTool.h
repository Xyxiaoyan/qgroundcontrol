#pragma once

#include "../AgentTool.h"

class ContextProvider;

class DistanceTool : public AgentTool
{
    Q_OBJECT
public:
    explicit DistanceTool(ContextProvider *ctx, QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("measure_distance"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override { return true; }

private:
    ContextProvider *_ctx = nullptr;
};
