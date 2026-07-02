#pragma once

#include "../AgentTool.h"

class LogAnalyzer;

class LogAnalysisTool : public AgentTool
{
    Q_OBJECT
public:
    explicit LogAnalysisTool(LogAnalyzer *analyzer, QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("analyze_flight_log"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override { return true; }

private:
    LogAnalyzer *_analyzer = nullptr;
};
