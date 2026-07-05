#pragma once

#include "../AgentTool.h"

class AltitudeSpeedTool : public AgentTool
{
    Q_OBJECT
public:
    explicit AltitudeSpeedTool(QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("altitude_speed"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override { return false; }
};
