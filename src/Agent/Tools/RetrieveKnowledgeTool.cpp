#include "RetrieveKnowledgeTool.h"
#include "../CloudRAGClient.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>

RetrieveKnowledgeTool::RetrieveKnowledgeTool(CloudRAGClient *rag, QObject *parent)
    : AgentTool(parent)
    , _rag(rag)
{
}

QString RetrieveKnowledgeTool::description() const
{
    return QStringLiteral(
        "检索无人机专业知识库。当问题涉及 PID 调参、传感器原理、故障排查、"
        "MAVLink 协议、飞行模式说明、安全规范等专业领域知识时调用此工具。"
        "返回与查询最相关的知识片段。\n"
        "参数:\n"
        "- query: 检索关键词或问题（必填）\n"
        "- top_k: 返回结果数量（可选，默认5）"
    );
}

QJsonObject RetrieveKnowledgeTool::parametersSchema() const
{
    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");

    QJsonObject props;
    QJsonObject queryProp;
    queryProp[QStringLiteral("type")] = QStringLiteral("string");
    queryProp[QStringLiteral("description")] = QStringLiteral("Search query text, e.g. 'PX4 MC_ROLL_P parameter tuning method'");
    props[QStringLiteral("query")] = queryProp;

    QJsonObject topKProp;
    topKProp[QStringLiteral("type")] = QStringLiteral("integer");
    topKProp[QStringLiteral("description")] = QStringLiteral("返回结果数量，默认5");
    topKProp[QStringLiteral("default")] = 5;
    props[QStringLiteral("top_k")] = topKProp;

    schema[QStringLiteral("properties")] = props;
    schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("query")};
    return schema;
}

QJsonObject RetrieveKnowledgeTool::execute(const QJsonObject &parameters)
{
    QJsonObject result;

    if (!_rag || !_rag->isEnabled()) {
        result[QStringLiteral("error")] = QStringLiteral("Knowledge base retrieval is not enabled. Please configure the RAG endpoint in settings");
        return result;
    }

    QString query = parameters.value(QStringLiteral("query")).toString();
    if (query.trimmed().isEmpty()) {
        result[QStringLiteral("error")] = QStringLiteral("query 参数不能为空");
        return result;
    }

    int topK = parameters.value(QStringLiteral("top_k")).toInt(5);

    QList<CloudRAGClient::RAGResult> results = _rag->retrieveSync(query, topK);

    if (results.isEmpty()) {
        result[QStringLiteral("status")] = QStringLiteral("no_results");
        result[QStringLiteral("result")] = QStringLiteral("No relevant knowledge snippets found");
        return result;
    }

    // Build results array
    QJsonArray chunks;
    for (const auto &r : results) {
        QJsonObject chunk;
        chunk[QStringLiteral("content")] = r.content;
        chunk[QStringLiteral("score")] = r.score;
        chunk[QStringLiteral("source")] = r.source.isEmpty() ? r.title : r.source;
        chunks.append(chunk);
    }

    result[QStringLiteral("status")] = QStringLiteral("success");
    result[QStringLiteral("num_results")] = results.size();
    result[QStringLiteral("chunks")] = chunks;
    result[QStringLiteral("result")] = QStringLiteral("Retrieved %1 relevant knowledge snippets").arg(results.size());
    return result;
}
