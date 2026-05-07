#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

class QNetworkAccessManager;
class QNetworkReply;

class LLMClient : public QObject
{
    Q_OBJECT
public:
    explicit LLMClient(QObject *parent = nullptr);
    ~LLMClient();

    void sendRequest(const QString &endpoint, const QString &apiKey, const QString &model,
                     const QString &systemPrompt, const QStringList &history);
    void cancelRequest();

signals:
    void responseReceived(const QString &content);
    void streamChunk(const QString &text);
    void requestError(const QString &error);

private slots:
    void _onReplyFinished();

private:
    QByteArray _buildBody(const QString &model, const QString &systemPrompt, const QStringList &history) const;
    QString _extractContent(const QByteArray &data) const;

    QNetworkAccessManager *_network = nullptr;
    QNetworkReply *_currentReply = nullptr;

    static constexpr int kTimeoutMs = 15000;
};
