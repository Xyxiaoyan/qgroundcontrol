#include "CloudRAGClient.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QUrl>
#include <QtCore/QEventLoop>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

QGC_LOGGING_CATEGORY(CloudRAGLog, "qgc.agent.rag")

CloudRAGClient::CloudRAGClient(QObject *parent)
    : QObject(parent)
    , _network(new QNetworkAccessManager(this))
{
}

CloudRAGClient::~CloudRAGClient() = default;

void CloudRAGClient::setApiKey(const QString &key)
{
    _apiKey = key;
}

void CloudRAGClient::setRagEndpoint(const QString &url)
{
    _ragEndpoint = url.trimmed();
    while (_ragEndpoint.endsWith(QLatin1Char('/'))) {
        _ragEndpoint.chop(1);
    }
}

void CloudRAGClient::setEnabled(bool enabled)
{
    _enabled = enabled;
}

QList<CloudRAGClient::RAGResult> CloudRAGClient::retrieveSync(const QString &query, int topK, qreal scoreThreshold)
{
    if (!_enabled) {
        return {};
    }
    if (_ragEndpoint.isEmpty()) {
        qCWarning(CloudRAGLog) << "RAG endpoint not configured";
        return {};
    }
    if (_apiKey.isEmpty()) {
        qCWarning(CloudRAGLog) << "RAG API key not set";
        return {};
    }
    if (query.trimmed().isEmpty()) {
        return {};
    }

    // Build request body (Dify-compatible format)
    QJsonObject body;
    body[QStringLiteral("query")] = query;

    QJsonObject retrievalSetting;
    retrievalSetting[QStringLiteral("top_k")] = topK;
    retrievalSetting[QStringLiteral("score_threshold")] = scoreThreshold;
    body[QStringLiteral("retrieval_setting")] = retrievalSetting;

    const QByteArray postData = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QUrl url(_ragEndpoint);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(_apiKey).toUtf8());
    request.setTransferTimeout(kTimeoutMs);

    qCDebug(CloudRAGLog) << "RAG retrieve:" << query.left(80) << "→" << _ragEndpoint;

    QNetworkReply *reply = _network->post(request, postData);
    if (!reply) {
        qCWarning(CloudRAGLog) << "Failed to create network reply";
        return {};
    }

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        int sc = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString err = QStringLiteral("RAG retrieve failed (%1): %2")
                          .arg(sc)
                          .arg(reply->errorString().left(200));
        qCWarning(CloudRAGLog) << err;
        emit retrieveError(err);
        return {};
    }

    QByteArray responseData = reply->readAll();
    QList<RAGResult> results = _parseResponse(responseData);

    qCDebug(CloudRAGLog) << "RAG retrieved" << results.size() << "chunks for query:" << query.left(50);
    return results;
}

QList<CloudRAGClient::RAGResult> CloudRAGClient::_parseResponse(const QByteArray &data) const
{
    QList<RAGResult> results;

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        qCWarning(CloudRAGLog) << "RAG response JSON parse failed:" << parseErr.errorString();
        return {};
    }

    QJsonObject root = doc.object();

    // Strategy 1: Dify format — { "records": [ { "content", "score", "title" } ] }
    QJsonArray records = root[QStringLiteral("records")].toArray();
    if (!records.isEmpty()) {
        for (const QJsonValue &item : records) {
            QJsonObject obj = item.toObject();
            RAGResult r;
            r.content = obj[QStringLiteral("content")].toString();
            r.score = obj[QStringLiteral("score")].toDouble();
            r.title = obj[QStringLiteral("title")].toString();
            r.source = obj[QStringLiteral("metadata")].toObject()
                           .value(QStringLiteral("source")).toString();
            if (r.source.isEmpty()) {
                r.source = r.title;
            }
            if (!r.content.isEmpty()) {
                results.append(r);
            }
        }
        return results;
    }

    // Strategy 2: Generic array — { "data": [ { "content", "score" } ] }
    QJsonArray dataArr = root[QStringLiteral("data")].toArray();
    if (!dataArr.isEmpty()) {
        for (const QJsonValue &item : dataArr) {
            QJsonObject obj = item.toObject();
            RAGResult r;
            r.content = obj[QStringLiteral("content")].toString();
            if (r.content.isEmpty()) {
                r.content = obj[QStringLiteral("text")].toString();
            }
            r.score = obj[QStringLiteral("score")].toDouble();
            r.title = obj[QStringLiteral("title")].toString();
            r.source = obj[QStringLiteral("source")].toString();
            if (!r.content.isEmpty()) {
                results.append(r);
            }
        }
        return results;
    }

    // Strategy 3: DashScope Application format — { "output": { "text": "..." } }
    QJsonObject output = root[QStringLiteral("output")].toObject();
    QString text = output[QStringLiteral("text")].toString();
    if (!text.isEmpty()) {
        RAGResult r;
        r.content = text;
        r.score = 1.0;
        r.title = QStringLiteral("Knowledge base retrieval result");
        r.source = QStringLiteral("RAG Application");
        results.append(r);
        return results;
    }

    // Strategy 4: Direct array at root level — [ { "content", "score" } ]
    if (doc.isArray()) {
        QJsonArray arr = doc.array();
        for (const QJsonValue &item : arr) {
            QJsonObject obj = item.toObject();
            RAGResult r;
            r.content = obj[QStringLiteral("content")].toString();
            if (r.content.isEmpty()) {
                r.content = obj[QStringLiteral("text")].toString();
            }
            r.score = obj[QStringLiteral("score")].toDouble();
            r.title = obj[QStringLiteral("title")].toString();
            if (!r.content.isEmpty()) {
                results.append(r);
            }
        }
        return results;
    }

    qCWarning(CloudRAGLog) << "RAG response format not recognized. First 200 bytes:"
                           << QString::fromUtf8(data.left(200));
    return {};
}

QString CloudRAGClient::formatContext(const QList<RAGResult> &results)
{
    if (results.isEmpty()) {
        return {};
    }

    QString text = QStringLiteral("\n\n## Relevant Knowledge Base Snippets\n\n");
    for (int i = 0; i < results.size(); ++i) {
        const RAGResult &r = results[i];
        text += QStringLiteral("[%1] ").arg(i + 1);
        if (!r.source.isEmpty()) {
            text += QStringLiteral("(Source: %1").arg(r.source);
            if (r.score > 0) {
                text += QStringLiteral(", Score: %1").arg(r.score, 0, 'f', 2);
            }
            text += QStringLiteral(")\n");
        }
        text += r.content + QStringLiteral("\n\n");
    }
    return text;
}
