#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QList>
#include <QtCore/QJsonObject>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * Cloud RAG retrieval client.
 *
 * Calls a cloud-hosted RAG API to retrieve relevant knowledge chunks
 * for a given query. Designed to work with Dify-style retrieve endpoints
 * (POST JSON, Bearer auth, returns array of {content, score, title}).
 *
 * The retrieve endpoint URL and API key are configurable. By default the
 * API key reuses the main LLM API key (same provider).
 *
 * Usage:
 *   client->setRagEndpoint("https://dify.example.com/v1/datasets/{id}/retrieve");
 *   client->setApiKey("sk-...");
 *   auto results = client->retrieveSync("PX4 PID tuning", 5);
 */
class CloudRAGClient : public QObject
{
    Q_OBJECT
public:
    struct RAGResult {
        QString content;
        qreal score = 0.0;
        QString title;
        QString source;
    };

    explicit CloudRAGClient(QObject *parent = nullptr);
    ~CloudRAGClient();

    // Configuration
    void setApiKey(const QString &key);
    void setRagEndpoint(const QString &url);
    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled; }
    QString ragEndpoint() const { return _ragEndpoint; }
    QString apiKey() const { return _apiKey; }

    // Synchronous retrieval (blocks until HTTP response, uses QEventLoop).
    // Returns empty list on error. Suitable for tool execution and auto-inject.
    QList<RAGResult> retrieveSync(const QString &query, int topK = 5, qreal scoreThreshold = 0.3);

    // Format results as injectable context text
    static QString formatContext(const QList<RAGResult> &results);

signals:
    void retrieveError(const QString &error);

private:
    QList<RAGResult> _parseResponse(const QByteArray &data) const;

    QNetworkAccessManager *_network = nullptr;
    QString _apiKey;
    QString _ragEndpoint;
    bool _enabled = false;

    static constexpr int kTimeoutMs = 15000;
};
