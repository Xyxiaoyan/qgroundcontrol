#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

class QNetworkAccessManager;
class QNetworkReply;

class LLMClient : public QObject
{
    Q_OBJECT
public:
    explicit LLMClient(QObject *parent = nullptr);
    ~LLMClient();

    void sendRequest(const QString &endpoint, const QString &apiKey, const QString &model,
                     const QString &systemPrompt, const QJsonArray &history, bool stream = false);
    void sendFunctionRequest(const QString &endpoint, const QString &apiKey, const QString &model,
                              const QString &systemPrompt, const QJsonArray &messages,
                              const QJsonArray &tools, bool stream = false);
    void sendVisionRequest(const QString &endpoint, const QString &apiKey, const QString &model,
                           const QString &prompt, const QString &imageBase64);
    void fetchModels(const QString &endpoint, const QString &apiKey);
    void cancelRequest();

    // Rough token estimation (~4 chars/token for English text)
    static int estimateTokens(const QString &text);
    static int estimateMessagesTokens(const QJsonArray &messages);

signals:
    void responseReceived(const QString &content);
    void responseReceivedWithTools(const QString &content, const QJsonObject &functionCall);
    void streamChunk(const QString &text);
    void thinkingChunk(const QString &text);
    void requestError(const QString &error);
    void modelsFetched(const QJsonArray &models);

private slots:
    void _onReplyFinished();
    void _onStreamReadyRead();

private:
    QByteArray _buildBody(const QString &model, const QString &systemPrompt, const QJsonArray &history, bool stream) const;
    QByteArray _buildFunctionBody(const QString &model, const QString &systemPrompt,
                                   const QJsonArray &messages, const QJsonArray &tools, bool stream) const;
    QByteArray _buildVisionBody(const QString &model, const QString &prompt, const QString &imageBase64) const;
    QString _extractContent(const QByteArray &data) const;
    QString _extractContentWithTools(const QByteArray &data, QJsonObject &functionCall) const;
    void _processStreamEvent(const QByteArray &event);

    QNetworkAccessManager *_network = nullptr;
    QPointer<QNetworkReply> _currentReply;
    QByteArray _streamBuffer;

    // Stream state for function calling
    struct StreamState {
        QString functionName;
        QString functionArguments;
        QString contentBuffer;
        QString reasoningContent;
        bool inFunctionCall = false;
        void reset() { functionName.clear(); functionArguments.clear(); contentBuffer.clear(); reasoningContent.clear(); inFunctionCall = false; }
    };
    StreamState _streamState;

    static constexpr int kTimeoutMs = 30000;
};
