#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <QtCore/QJsonArray>

#include "LogAnalysisModel.h"

class LLMClient;
class ContextProvider;
class ConfigStore;
class ConversationHistory;
class Vehicle;
class LogAnalyzer;
class OnboardLogController;
class ReActLoop;
class ToolRegistry;

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
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY apiKeyChanged)
    Q_PROPERTY(QString apiEndpoint READ apiEndpoint WRITE setApiEndpoint NOTIFY apiEndpointChanged)
    Q_PROPERTY(QString modelName READ modelName WRITE setModelName NOTIFY modelNameChanged)
    Q_PROPERTY(QString geocodeApiKey READ geocodeApiKey WRITE setGeocodeApiKey NOTIFY geocodeApiKeyChanged)
    Q_PROPERTY(bool isAutonomous READ isAutonomous NOTIFY isAutonomousChanged)
    Q_PROPERTY(int totalTokens READ totalTokens NOTIFY totalTokensChanged)
    Q_PROPERTY(QString streamingText READ streamingText NOTIFY streamingTextChanged)
    Q_PROPERTY(QString thinkingText READ thinkingText NOTIFY thinkingTextChanged)
    Q_PROPERTY(LogAnalysisModel *logAnalysisModel READ logAnalysisModel NOTIFY logAnalysisModelChanged)

    bool isProcessing() const { return _isProcessing; }
    QString errorMessage() const { return _errorMessage; }
    QString statusText() const { return _statusText; }
    bool isAutonomous() const { return _isAutonomous; }
    int totalTokens() const { return _totalTokens; }
    QString streamingText() const { return _streamAccum; }
    QString thinkingText() const { return _thinkingAccum; }
    LogAnalysisModel *logAnalysisModel() const { return _logAnalysisModel; }

    Q_INVOKABLE void sendMessage(const QString &userMessage);
    Q_INVOKABLE void cancelRequest();
    Q_INVOKABLE void runQuickCheck(const QString &checkType);
    Q_INVOKABLE void setApiKey(const QString &key);
    Q_INVOKABLE void setApiEndpoint(const QString &url);
    Q_INVOKABLE void setModelName(const QString &model);
    Q_INVOKABLE QString apiKey() const { return _apiKey; }
    Q_INVOKABLE QString apiEndpoint() const { return _apiEndpoint; }
    Q_INVOKABLE QString baseApiEndpoint() const;
    Q_INVOKABLE QString modelName() const { return _modelName; }
    Q_INVOKABLE QString geocodeApiKey() const { return _geocodeApiKey; }
    Q_INVOKABLE void setGeocodeApiKey(const QString &key);
    Q_INVOKABLE void sendVisionQuery(const QString &prompt, const QString &imageBase64);
    Q_INVOKABLE void startAutonomousTask(const QString &task);
    Q_INVOKABLE void stopAutonomousTask();
    Q_INVOKABLE void setAutonomous(bool autonomous);
    Q_INVOKABLE void runLogAnalysis(const QString &filePath, const QString &analysisType);
    Q_INVOKABLE void listDownloadedLogs();
    Q_INVOKABLE void downloadOnboardLogs();
    Q_INVOKABLE QJsonArray listOnboardLogs();
    Q_INVOKABLE void downloadOnboardLogByIndex(int index);
    Q_INVOKABLE void analyzeLogByIndex(int index);
    Q_INVOKABLE void fetchModels();
    Q_INVOKABLE void clearConversationHistory();
    Q_INVOKABLE bool hasConversationHistory() const;

signals:
    void messageReceived(const QString &role, const QString &content);
    void streamingChunk(const QString &text);
    void isProcessingChanged();
    void errorMessageChanged();
    void statusTextChanged();
    void vehicleConnectedChanged(bool connected);
    void commandPending(const QString &description, const QVariantMap &cmdMap);
    void commandExecuted(const QString &result);
    void commandFailed(const QString &error);
    void apiKeyChanged();
    void apiEndpointChanged();
    void modelNameChanged();
    void geocodeApiKeyChanged();
    void isAutonomousChanged();
    void totalTokensChanged();
    void streamingTextChanged();
    void thinkingTextChanged();
    void logAnalysisModelChanged();
    void autonomousStep(const QString &thought, const QString &action);
    void logAnalysisReady(const QString &report);
    void downloadedLogsList(const QJsonArray &logs);
    void onboardDownloadProgress(const QString &status);
    void onboardDownloadComplete(const QString &filePath);
    void modelsFetched(const QJsonArray &models);

public slots:
    Q_INVOKABLE void confirmCommand(const QVariantMap &cmdMap);
    Q_INVOKABLE void rejectCommand();

private slots:
    void _onReActStarted();
    void _onReActStopped();
    void _onReActMessageProduced(const QString &role, const QString &content);
    void _onReActStreamChunk(const QString &text);
    void _onReActThinkingChunk(const QString &text);
    void _onReActToolConfirmationRequired(const QString &toolName, const QString &description, const QJsonObject &parameters);
    void _onReActToolResult(const QString &toolName, const QJsonObject &observation);
    void _onReActThought(const QString &thought);
    void _onReActToolInvoked(const QString &toolName, const QJsonObject &parameters);
    void _onReActFinalAnswer(const QString &answer);
    void _onVisionResponse(const QString &content);
    void _onVisionError(const QString &error);
    void _onOnboardLogListComplete();
    void _onOnboardLogDownloadComplete();

private:
    void _trackTokens(const QString &text);
    QString _buildCommandDescription(const QString &toolName, const QJsonObject &parameters) const;

    LLMClient *_llm = nullptr;
    ContextProvider *_ctx = nullptr;
    ConfigStore *_config = nullptr;
    ConversationHistory *_history = nullptr;
    LogAnalyzer *_logAnalyzer = nullptr;
    LogAnalysisModel *_logAnalysisModel = nullptr;
    OnboardLogController *_onboardLogCtrl = nullptr;
    ToolRegistry *_toolRegistry = nullptr;
    ReActLoop *_reactLoop = nullptr;

    QJsonArray _downloadedLogsCache;
    QString _onboardDownloadDir;

    QString _apiEndpoint;
    QString _apiKey;
    QString _geocodeApiKey;
    QString _modelName;
    bool _isProcessing = false;
    QString _errorMessage;
    QString _statusText;
    bool _isAutonomous = false;

    QJsonArray _chatMessages;
    QString _streamAccum;
    QString _thinkingAccum;
    QTimer *_thinkThrottle = nullptr;
    int _totalTokens = 0;
    bool _inVisionQuery = false;
    QString _lastThought;
    QString _lastToolAction;
    static constexpr int kDailyTokenLimit = 200000;
};
