#pragma once

#include "../AgentTool.h"

class ContextProvider;

class POISearchTool : public AgentTool
{
    Q_OBJECT
public:
    explicit POISearchTool(ContextProvider *ctx, QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("search_poi"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override { return true; }

private:
    ContextProvider *_ctx = nullptr;
};
