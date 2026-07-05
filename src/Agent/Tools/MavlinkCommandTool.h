#pragma once

#include "../AgentTool.h"

#include <QtCore/QHash>
#include <all/all.h>

class MavlinkCommandTool : public AgentTool
{
    Q_OBJECT
public:
    explicit MavlinkCommandTool(QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("mavlink_command"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override { return false; }

private:
    static QHash<QString, MAV_CMD> _buildCmdMap();
    static const QHash<QString, MAV_CMD> kCmdMap;
};
