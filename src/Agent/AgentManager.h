#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariantMap>

#include "CommandEngine.h"

class LLMClient;
class ContextProvider;
class Vehicle;

class AgentManager : public QObject
{
    Q_OBJECT
public:
    static AgentManager *instance();
    explicit AgentManager(QObject *parent = nullptr);
    ~AgentManager();

    Q_PROPERTY(bool isProcessing READ isProcessing NOTIFY isProcessingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

    bool isProcessing() const { return _isProcessing; }
    QString errorMessage() const { return _errorMessage; }
    QString statusText() const { return _statusText; }

    Q_INVOKABLE void sendMessage(const QString &userMessage);
    Q_INVOKABLE void cancelRequest();
    Q_INVOKABLE void runQuickCheck(const QString &checkType);
    Q_INVOKABLE void setApiKey(const QString &key);
    Q_INVOKABLE void setApiEndpoint(const QString &url);
    Q_INVOKABLE void setModelName(const QString &model);
    Q_INVOKABLE QString apiKey() const { return _apiKey; }
    Q_INVOKABLE QString apiEndpoint() const { return _apiEndpoint; }
    Q_INVOKABLE QString modelName() const { return _modelName; }

signals:
    void messageReceived(const QString &role, const QString &content);
    void streamingChunk(const QString &text);
    void isProcessingChanged();
    void errorMessageChanged();
    void statusTextChanged();
    void vehicleConnectedChanged(bool connected);
    void commandPending(const QString &description, const QVariantMap &cmdMap);

public slots:
    Q_INVOKABLE void confirmCommand(const QVariantMap &cmdMap);
    Q_INVOKABLE void rejectCommand();

private slots:
    void _onResponseReceived(const QString &content);
    void _onStreamChunk(const QString &text);
    void _onRequestError(const QString &error);

private:
    QString _buildSystemPrompt(const QString &userMessage);

    LLMClient *_llm = nullptr;
    ContextProvider *_ctx = nullptr;
    CommandEngine *_cmd = nullptr;
    AgentCommand _pendingCommand;

    QString _apiEndpoint = QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions");
    QString _apiKey;
    QString _modelName = QStringLiteral("qwen-plus");
    bool _isProcessing = false;
    QString _errorMessage;
    QString _statusText;
    QStringList _chatHistory;

    static constexpr int kMaxHistory = 20;
};
