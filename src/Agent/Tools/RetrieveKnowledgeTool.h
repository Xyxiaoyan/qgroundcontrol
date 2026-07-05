#pragma once

#include "../AgentTool.h"

class CloudRAGClient;

class RetrieveKnowledgeTool : public AgentTool
{
    Q_OBJECT
public:
    explicit RetrieveKnowledgeTool(CloudRAGClient *rag, QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("retrieve_knowledge"); }
    QString description() const override;
    QJsonObject parametersSchema() const override;
    QJsonObject execute(const QJsonObject &parameters) override;
    bool isReadOnly() const override { return true; }

private:
    CloudRAGClient *_rag = nullptr;
};
