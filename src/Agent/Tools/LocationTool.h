#pragma once

#include "../AgentTool.h"

class ContextProvider;

class LocationTool : public AgentTool
{
    Q_OBJECT
public:
    explicit LocationTool(ContextProvider *ctx, QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("get_location_address"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override { return true; }

private:
    ContextProvider *_ctx = nullptr;
};
