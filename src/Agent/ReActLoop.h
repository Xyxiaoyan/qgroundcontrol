#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

class LLMClient;
class ToolRegistry;
class ContextProvider;
class ConversationHistory;
class AgentTool;
class CloudRAGClient;

class ReActLoop : public QObject
{
    Q_OBJECT
public:
    explicit ReActLoop(LLMClient *llm, ToolRegistry *tools, ContextProvider *ctx, QObject *parent = nullptr);

    Q_INVOKABLE void start(const QString &userMessage, bool autonomous = false);
    Q_INVOKABLE void cancel();

    bool isRunning() const { return _isRunning; }
    int currentIteration() const { return _iteration; }
    int maxIterations() const { return _maxIterations; }
    void setMaxIterations(int n) { _maxIterations = n; }
    bool isAutonomous() const { return _autonomous; }
    void setAutonomous(bool autonomous) { _autonomous = autonomous; }
    bool isAwaitingConfirmation() const { return _awaitingConfirmation; }

    Q_INVOKABLE void confirmToolExecution();
    Q_INVOKABLE void rejectToolExecution();

    void setConversationHistory(ConversationHistory *history);
    void setRagClient(CloudRAGClient *rag) { _rag = rag; }
    Q_INVOKABLE void clearHistory();
    bool hasHistory() const;

signals:
    void streamingChunk(const QString &text);
    void thinkingChunk(const QString &text);
    void thoughtProduced(const QString &thought);
    void toolInvoked(const QString &toolName, const QJsonObject &parameters);
    void toolResult(const QString &toolName, const QJsonObject &observation);
    void finalAnswer(const QString &answer);
    void toolConfirmationRequired(const QString &toolName, const QString &description, const QJsonObject &parameters);
    void started();
    void stopped();
    void iterationCompleted(int iteration, int total);
    void messageProduced(const QString &role, const QString &content);
    void functionCallReceived(const QString &toolName, const QJsonObject &arguments);

private slots:
    void _onLLMResponse(const QString &content);
    void _onLLMResponseWithTools(const QString &content, const QJsonObject &functionCall);
    void _onStreamChunk(const QString &text);
    void _onThinkingChunk(const QString &text);
    void _onLLMError(const QString &error);

private:
    void _step();
    void _sendToLLM();
    void _executeTool(const QString &toolName, const QJsonObject &arguments);
    void _handleFinalResponse(const QString &content);
    void _appendMessage(const QJsonObject &msg);
    void _appendToolResult(const QString &toolName, const QJsonObject &observation);
    void _trimForContextLimit();
    QString _buildSystemPrompt() const;
    QJsonObject _checkToolSafety(const QString &toolName, const QJsonObject &arguments, AgentTool *tool);

    LLMClient *_llm;
    ToolRegistry *_tools;
    ContextProvider *_ctx;
    ConversationHistory *_history = nullptr;
    CloudRAGClient *_rag = nullptr;

    QJsonArray _messages;
    int _iteration = 0;
    int _maxIterations = 0;  // 0 = no limit, model decides when to stop
    int _maxContextTokens = 28000;  // Context window budget (leaves room for response)
    bool _isRunning = false;
    bool _autonomous = false;
    bool _awaitingConfirmation = false;
    QString _thinkingAccum;

    // Pending tool call awaiting user confirmation
    QString _pendingToolName;
    QJsonObject _pendingToolArgs;
    QString _toolCallId;

    // Config (set externally or via setters)
    QString _apiEndpoint;
    QString _apiKey;
    QString _modelName;

public:
    void setApiEndpoint(const QString &url) { _apiEndpoint = url; }
    void setApiKey(const QString &key) { _apiKey = key; }
    void setModelName(const QString &model) { _modelName = model; }
};
