#pragma once

#include "../AgentTool.h"

class RelativeMoveTool : public AgentTool
{
    Q_OBJECT
public:
    explicit RelativeMoveTool(QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("relative_move"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override { return false; }
};
