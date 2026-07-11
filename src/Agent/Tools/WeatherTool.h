#pragma once

#include "../AgentTool.h"

class ContextProvider;

class WeatherTool : public AgentTool
{
    Q_OBJECT
public:
    explicit WeatherTool(ContextProvider *ctx, QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("query_weather"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override { return true; }

private:
    ContextProvider *_ctx = nullptr;
};
