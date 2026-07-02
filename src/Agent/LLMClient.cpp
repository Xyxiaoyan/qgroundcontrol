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
{}

LLMClient::~LLMClient()
{
    cancelRequest();
}

void LLMClient::sendRequest(const QString &endpoint, const QString &apiKey, const QString &model,
                             const QString &systemPrompt, const QJsonArray &history, bool stream)
{
    if (_currentReply && _currentReply->isRunning()) {
        cancelRequest();
    }

    const QByteArray body = _buildBody(model, systemPrompt, history, stream);

    QUrl url(endpoint);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey).toUtf8());
    request.setTransferTimeout(stream ? 120000 : kTimeoutMs);

    _streamBuffer.clear();
    _streamState.reset();
    _currentReply = _network->post(request, body);

    if (stream) {
        connect(_currentReply, &QNetworkReply::readyRead, this, &LLMClient::_onStreamReadyRead);
        connect(_currentReply, &QNetworkReply::finished, this, &LLMClient::_onReplyFinished);
    } else {
        connect(_currentReply, &QNetworkReply::finished, this, &LLMClient::_onReplyFinished);
    }
}

void LLMClient::sendFunctionRequest(
    const QString &endpoint,
    const QString &apiKey,
    const QString &model,
    const QString &systemPrompt,
    const QJsonArray &messages,
    const QJsonArray &tools,
    bool stream)
{
    if (_currentReply && _currentReply->isRunning()) {
        cancelRequest();
    }

    const QByteArray body = _buildFunctionBody(model, systemPrompt, messages, tools, stream);

    qCDebug(LLMClientLog) << "Function request: model=" << model << "msgs=" << messages.size() << "tools=" << tools.size();

    QUrl url(endpoint);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey).toUtf8());
    request.setTransferTimeout(stream ? 120000 : kTimeoutMs);

    _streamBuffer.clear();
    _streamState.reset();
    _currentReply = _network->post(request, body);

    if (stream) {
        connect(_currentReply, &QNetworkReply::readyRead, this, &LLMClient::_onStreamReadyRead);
        connect(_currentReply, &QNetworkReply::finished, this, &LLMClient::_onReplyFinished);
    } else {
        connect(_currentReply, &QNetworkReply::finished, this, &LLMClient::_onReplyFinished);
    }
}

void LLMClient::sendVisionRequest(
    const QString &endpoint,
    const QString &apiKey,
    const QString &model,
    const QString &prompt,
    const QString &imageBase64)
{
    if (_currentReply && _currentReply->isRunning()) {
        cancelRequest();
    }

    const QByteArray body = _buildVisionBody(model, prompt, imageBase64);

    QUrl url(endpoint);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey).toUtf8());
    request.setTransferTimeout(kTimeoutMs);

    _currentReply = _network->post(request, body);
    connect(_currentReply, &QNetworkReply::finished, this, &LLMClient::_onReplyFinished);
}

void LLMClient::fetchModels(const QString &endpoint, const QString &apiKey)
{
    // Derive models URL from chat completions endpoint
    // e.g. https://api.deepseek.com/v1/chat/completions → https://api.deepseek.com/v1/models
    //      https://open.bigmodel.cn/api/paas/v4/chat/completions → https://open.bigmodel.cn/api/paas/v4/models
    QString baseUrl = endpoint;
    // Strip /chat/completions suffix (handles both /v1/chat/completions and /v4/chat/completions)
    if (baseUrl.endsWith(QStringLiteral("/chat/completions"))) {
        baseUrl = baseUrl.left(baseUrl.size() - 18);
    }
    // baseUrl is now the API base (e.g. .../v1 or .../v4)
    // Append /models
    QString modelsUrl = baseUrl + QStringLiteral("/models");

    QUrl url(modelsUrl);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey).toUtf8());
    request.setTransferTimeout(kTimeoutMs);

    QNetworkReply *reply = _network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError(QStringLiteral("获取模型列表失败: %1").arg(reply->errorString()));
            return;
        }

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error != QJsonParseError::NoError) {
            emit requestError(QStringLiteral("解析模型列表失败"));
            return;
        }

        QJsonArray data = doc.object()[QStringLiteral("data")].toArray();
        QJsonArray models;
        for (const QJsonValue &item : data) {
            QString id = item[QStringLiteral("id")].toString();
            if (!id.isEmpty()) {
                models.append(id);
            }
        }
        if (models.isEmpty()) {
            emit requestError(QStringLiteral("未找到可用模型"));
            return;
        }
        emit modelsFetched(models);
    });
}

void LLMClient::cancelRequest()
{
    if (_currentReply) {
        // Save and detach BEFORE abort() — abort() may synchronously fire
        // _onReplyFinished which sets _currentReply = nullptr. Dereferencing
        // _currentReply after that point crashes (null QPointer::operator->).
        QPointer<QNetworkReply> reply = _currentReply;
        _currentReply = nullptr;
        // Disconnect signals to prevent any queued callbacks from firing
        // on a reply we're about to abort and destroy.
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    _streamBuffer.clear();
    _streamState.reset();
}

QByteArray LLMClient::_buildBody(const QString &model, const QString &systemPrompt,
                                  const QJsonArray &history, bool stream) const
{
    QJsonArray messages;
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), systemPrompt}
    });

    static constexpr int kMaxHistory = 16;
    int start = qMax(0, history.size() - kMaxHistory);
    for (int i = start; i < history.size(); i++) {
        messages.append(history[i].toObject());
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("stream"), stream);
    body.insert(QStringLiteral("temperature"), stream ? 0.7 : 0.3);

    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QByteArray LLMClient::_buildFunctionBody(
    const QString &model,
    const QString &systemPrompt,
    const QJsonArray &messages,
    const QJsonArray &tools,
    bool stream) const
{
    QJsonArray msgs;

    // Only prepend system prompt if it's non-empty AND messages don't already start with one
    bool hasSystem = !messages.isEmpty() && messages.first().toObject()[QStringLiteral("role")].toString() == QStringLiteral("system");
    if (!systemPrompt.isEmpty() && !hasSystem) {
        msgs.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("system")},
            {QStringLiteral("content"), systemPrompt}
        });
    }

    // Append all messages from the provided array
    for (int i = 0; i < messages.size(); i++) {
        msgs.append(messages[i].toObject());
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("messages"), msgs);
    body.insert(QStringLiteral("stream"), stream);
    body.insert(QStringLiteral("tools"), tools);

    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QByteArray LLMClient::_buildVisionBody(const QString &model, const QString &prompt,
                                        const QString &imageBase64) const
{
    QJsonObject imageUrlObj;
    imageUrlObj.insert(QStringLiteral("url"), QStringLiteral("data:image/jpeg;base64,") + imageBase64);

    QJsonObject imageObj;
    imageObj.insert(QStringLiteral("type"), QStringLiteral("image_url"));
    imageObj.insert(QStringLiteral("image_url"), imageUrlObj);

    QJsonObject textObj;
    textObj.insert(QStringLiteral("type"), QStringLiteral("text"));
    textObj.insert(QStringLiteral("text"), prompt);

    QJsonArray contentArray;
    contentArray.append(textObj);
    contentArray.append(imageObj);

    QJsonObject userMsg;
    userMsg.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMsg.insert(QStringLiteral("content"), contentArray);

    QString sysPrompt = QStringLiteral(
        "You are a drone vision navigation assistant. Analyze the incoming real-time aerial image.\n\n"
        "You can issue flight control commands using <cmd> tags (user confirms before execution):\n"
        "  Body-frame movement (relative to heading):\n"
        "    <cmd>fwd [meters]</cmd> Move forward\n"
        "    <cmd>back [meters]</cmd> Move backward\n"
        "    <cmd>left [meters]</cmd> Move left\n"
        "    <cmd>right [meters]</cmd> Move right\n"
        "    <cmd>turn [degrees]</cmd> Rotate in place\n"
        "  Absolute commands:\n"
        "    <cmd>goto lat lon [alt]</cmd> Fly to coordinates\n"
        "    <cmd>alt [+/-]value</cmd> Change altitude (relative)\n"
        "    <cmd>speed value</cmd> Set ground speed (m/s)\n"
        "    <cmd>land</cmd> Land immediately\n"
        "    <cmd>rtl</cmd> Return to launch\n"
        "    <cmd>pause</cmd> Hover in place\n"
        "  Flight modes (ArduPilot Copter):\n"
        "    <cmd>mode GUIDED</cmd> Guided mode\n"
        "    <cmd>mode LOITER</cmd> Loiter\n"
        "    <cmd>mode RTL</cmd> Return to launch\n"
        "    <cmd>mode LAND</cmd> Land mode\n"
        "    <cmd>mode ALT_HOLD</cmd> Altitude hold\n"
        "    <cmd>mode BRAKE</cmd> Emergency brake\n\n"
        "Be thorough: describe what you see, identify hazards and key features, and give actionable advice.\n"
        "If the user asks for navigation, provide specific flight directions with <cmd> tags.\n"
        "If you detect hazards (obstacles, power lines, crowds), warn prominently.\n"
        "IMPORTANT: Always respond in Chinese. Use structured formatting for clarity."
    );

    QJsonArray msgs;
    msgs.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), sysPrompt}
    });
    msgs.append(userMsg);

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("messages"), msgs);
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

    QString content = message[QStringLiteral("content")].toString();
    QString reasoning = message[QStringLiteral("reasoning_content")].toString();
    if (content.isEmpty() && !reasoning.isEmpty()) {
        content = reasoning;
    }
    return content;
}

QString LLMClient::_extractContentWithTools(const QByteArray &data, QJsonObject &functionCall) const
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

    // Extract function_call (OpenAI format)
    QJsonObject fc = message[QStringLiteral("function_call")].toObject();
    if (fc.isEmpty()) {
        // Also check tool_calls format
        QJsonArray toolCalls = message[QStringLiteral("tool_calls")].toArray();
        if (!toolCalls.isEmpty()) {
            fc = toolCalls.first().toObject().value(QStringLiteral("function")).toObject();
        }
    }
    if (!fc.isEmpty()) {
        functionCall = fc;
        // Normalize: some LLMs return arguments as a JSON object instead of a string.
        // Convert to string for consistent downstream parsing.
        if (functionCall.contains(QStringLiteral("arguments")) && functionCall[QStringLiteral("arguments")].isObject()) {
            QJsonObject argsObj = functionCall[QStringLiteral("arguments")].toObject();
            functionCall[QStringLiteral("arguments")] = QString::fromUtf8(
                QJsonDocument(argsObj).toJson(QJsonDocument::Compact));
        }
        // Pass reasoning content for thinking-mode models
        QString reasoning = message[QStringLiteral("reasoning_content")].toString();
        if (!reasoning.isEmpty()) {
            functionCall[QStringLiteral("_reasoning")] = reasoning;
        }
    }

    QString content = message[QStringLiteral("content")].toString();
    QString reasoning = message[QStringLiteral("reasoning_content")].toString();
    if (content.isEmpty() && !reasoning.isEmpty()) {
        content = reasoning;
    }
    return content;
}

void LLMClient::_processStreamEvent(const QByteArray &event)
{
    // Parse a single SSE event (data: {...})
    QString text = QString::fromUtf8(event);
    for (const QString &line : text.split(QStringLiteral("\n"), Qt::SkipEmptyParts)) {
        if (!line.startsWith(QStringLiteral("data:"))) continue;
        QString jsonStr = line.mid(5).trimmed();
        if (jsonStr == QStringLiteral("[DONE]")) continue;

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError) continue;

        QJsonObject obj = doc.object();
        QJsonArray choices = obj[QStringLiteral("choices")].toArray();
        if (choices.isEmpty()) continue;

        QJsonObject choice = choices.first().toObject();
        QJsonObject delta = choice[QStringLiteral("delta")].toObject();

        // Check for function_call delta
        QJsonObject fcDelta = delta[QStringLiteral("function_call")].toObject();
        if (!fcDelta.isEmpty()) {
            _streamState.inFunctionCall = true;
            if (fcDelta.contains(QStringLiteral("name")) && !fcDelta[QStringLiteral("name")].isNull()) {
                _streamState.functionName += fcDelta[QStringLiteral("name")].toString();
            }
            if (fcDelta.contains(QStringLiteral("arguments")) && !fcDelta[QStringLiteral("arguments")].isNull()) {
                _streamState.functionArguments += fcDelta[QStringLiteral("arguments")].toString();
            }
            continue;
        }

        // Check for tool_calls delta (alternative format)
        QJsonArray tcArray = delta[QStringLiteral("tool_calls")].toArray();
        if (!tcArray.isEmpty()) {
            _streamState.inFunctionCall = true;
            QJsonObject tcFunc = tcArray.first().toObject().value(QStringLiteral("function")).toObject();
            if (tcFunc.contains(QStringLiteral("name")) && !tcFunc[QStringLiteral("name")].isNull()) {
                _streamState.functionName = tcFunc[QStringLiteral("name")].toString();
            }
            if (tcFunc.contains(QStringLiteral("arguments")) && !tcFunc[QStringLiteral("arguments")].isNull()) {
                _streamState.functionArguments += tcFunc[QStringLiteral("arguments")].toString();
            }
            continue;
        }

        // Content chunk
        QString chunk = delta[QStringLiteral("content")].toString();
        if (!chunk.isEmpty()) {
            _streamState.contentBuffer += chunk;
            if (!_streamState.inFunctionCall) {
                emit streamChunk(chunk);
            }
        }

        // Reasoning content (deepseek thinking mode) — separate signal for UI
        QString reasoningChunk = delta[QStringLiteral("reasoning_content")].toString();
        if (!reasoningChunk.isEmpty()) {
            _streamState.reasoningContent += reasoningChunk;
            if (!_streamState.inFunctionCall) {
                emit thinkingChunk(reasoningChunk);
            }
        }
    }
}

void LLMClient::_onStreamReadyRead()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || reply != _currentReply) return;

    QByteArray chunk = reply->readAll();
    _streamBuffer.append(chunk);

    // Parse complete SSE events from buffer
    while (true) {
        int idx = _streamBuffer.indexOf("\n\n");
        if (idx < 0) break;

        QByteArray event = _streamBuffer.left(idx);
        _streamBuffer = _streamBuffer.mid(idx + 2);
        _processStreamEvent(event);
    }
}

void LLMClient::_onReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || reply != _currentReply) {
        if (reply) reply->deleteLater();
        return;
    }

    _currentReply = nullptr;

    const QByteArray data = reply->readAll();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    qCDebug(LLMClientLog) << "API response: status=" << statusCode
                          << "error=" << reply->error() << "dataSize=" << data.size()
                          << "streamBuf=" << _streamBuffer.size();

    // Always check HTTP status first, with response body for debugging
    if (statusCode != 200 && statusCode != 0) {
        QString bodyPreview = QString::fromUtf8(data).left(300);
        emit requestError(QStringLiteral("API %1: %2").arg(statusCode).arg(bodyPreview));
        reply->deleteLater();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            QString bodyPreview = QString::fromUtf8(data).left(200);
            emit requestError(QStringLiteral("网络错误: %1 %2").arg(reply->errorString(), bodyPreview));
        }
        reply->deleteLater();
        return;
    }

    // For streaming requests, process remaining buffer
    bool wasStreaming = !_streamBuffer.isEmpty() || _streamState.inFunctionCall || !_streamState.contentBuffer.isEmpty();
    if (wasStreaming) {
        // Process any remaining SSE data in buffer
        if (!_streamBuffer.isEmpty()) {
            _processStreamEvent(_streamBuffer + "\n\n");
        }
        _streamBuffer.clear();

        if (_streamState.inFunctionCall) {
            // Build function call from accumulated stream state
            QJsonObject fc;
            fc[QStringLiteral("name")] = _streamState.functionName;
            fc[QStringLiteral("arguments")] = _streamState.functionArguments;
            // Pass reasoning content for thinking-mode models (e.g. deepseek-v4-pro)
            if (!_streamState.reasoningContent.isEmpty()) {
                fc[QStringLiteral("_reasoning")] = _streamState.reasoningContent;
            }
            emit responseReceivedWithTools(_streamState.contentBuffer, fc);
            _streamState.reset();
        } else {
            // Text-only streaming response.
            // Prefer contentBuffer — reasoningContent is the model's internal
            // chain-of-thought (already shown via thinkingChunk during streaming).
            // Only fall back to reasoningContent when there's no content at all.
            QString text = _streamState.contentBuffer.isEmpty()
                ? _streamState.reasoningContent
                : _streamState.contentBuffer;
            emit responseReceived(text);
            _streamState.reset();
        }
        reply->deleteLater();
        return;
    }

    if (data.isEmpty()) {
        reply->deleteLater();
        return;
    }

    // Non-streaming: check for function_call in response
    QJsonObject functionCall;
    const QString content = _extractContentWithTools(data, functionCall);
    if (!functionCall.isEmpty()) {
        emit responseReceivedWithTools(content, functionCall);
    } else {
        emit responseReceived(content);
    }

    reply->deleteLater();
}
