#include "LLMClient.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(LLMClientLog, "qgc.agent.llm")

LLMClient::LLMClient(QObject *parent)
    : QObject(parent)
    , _network(new QNetworkAccessManager(this))
{
    _network->setTransferTimeout(kTimeoutMs);
}

LLMClient::~LLMClient()
{
    cancelRequest();
}

void LLMClient::sendRequest(const QString &endpoint, const QString &apiKey, const QString &model,
                             const QString &systemPrompt, const QStringList &history)
{
    if (_currentReply && _currentReply->isRunning()) {
        cancelRequest();
    }

    const QByteArray body = _buildBody(model, systemPrompt, history);

    QUrl url(endpoint);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey).toUtf8());
    request.setTransferTimeout(kTimeoutMs);

    _currentReply = _network->post(request, body);
    connect(_currentReply, &QNetworkReply::finished, this, &LLMClient::_onReplyFinished);
}

void LLMClient::cancelRequest()
{
    if (_currentReply) {
        _currentReply->abort();
        _currentReply->deleteLater();
        _currentReply = nullptr;
    }
}

QByteArray LLMClient::_buildBody(const QString &model, const QString &systemPrompt,
                                  const QStringList &history) const
{
    QJsonArray messages;
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), systemPrompt}
    });

    int start = qMax(0, history.size() - 8);
    for (int i = start; i < history.size(); i++) {
        const QString &entry = history[i];
        int colon = entry.indexOf(':');
        QString role = (colon > 0) ? entry.left(colon) : QStringLiteral("user");
        QString content = (colon > 0) ? entry.mid(colon + 1) : entry;
        messages.append(QJsonObject{
            {QStringLiteral("role"), role},
            {QStringLiteral("content"), content}
        });
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("stream"), false);
    body.insert(QStringLiteral("temperature"), 0.3);

    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QString LLMClient::_extractContent(const QByteArray &data) const
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }
    const QJsonObject obj = doc.object();
    const QJsonArray choices = obj[QStringLiteral("choices")].toArray();
    if (choices.isEmpty()) return {};

    const QJsonObject choice = choices.first().toObject();
    const QJsonObject message = choice[QStringLiteral("message")].toObject();
    return message[QStringLiteral("content")].toString();
}

void LLMClient::_onReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || reply != _currentReply) {
        if (reply) reply->deleteLater();
        return;
    }

    _currentReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            emit requestError(QStringLiteral("网络错误: %1").arg(reply->errorString()));
        }
        reply->deleteLater();
        return;
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();

    if (statusCode != 200) {
        emit requestError(QStringLiteral("API 错误 %1: %2").arg(statusCode).arg(QString::fromUtf8(data).left(200)));
        reply->deleteLater();
        return;
    }

    const QString content = _extractContent(data);
    emit responseReceived(content);

    reply->deleteLater();
}
