#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QList>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtQmlIntegration/QtQmlIntegration>

class QNetworkAccessManager;
class QNetworkReply;
class Vehicle;

struct AgentMessage {
    QString role;
    QString content;
};

class AgentManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool     isProcessing    READ isProcessing   NOTIFY isProcessingChanged)
    Q_PROPERTY(QString  errorMessage    READ errorMessage   NOTIFY errorMessageChanged)
    Q_PROPERTY(QString  apiEndpoint     READ apiEndpoint    WRITE setApiEndpoint    NOTIFY apiEndpointChanged)
    Q_PROPERTY(QString  apiKey          READ apiKey         WRITE setApiKey         NOTIFY apiKeyChanged)
    Q_PROPERTY(QString  modelName       READ modelName      WRITE setModelName      NOTIFY modelNameChanged)

public:
    explicit AgentManager(QObject *parent = nullptr);
    ~AgentManager();

    static AgentManager *instance();

    bool isProcessing() const { return _isProcessing; }
    QString errorMessage() const { return _errorMessage; }
    QString apiEndpoint() const { return _apiEndpoint; }
    QString apiKey() const { return _apiKey; }
    QString modelName() const { return _modelName; }

    void setApiEndpoint(const QString &endpoint);
    void setApiKey(const QString &key);
    void setModelName(const QString &model);

    Q_INVOKABLE void sendMessage(const QString &userMessage);
    Q_INVOKABLE void cancelRequest();
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void runQuickCheck(const QString &checkType);
    Q_INVOKABLE QString buildSystemContext();

signals:
    void messageReceived(const QString &role, const QString &content);
    void streamingChunk(const QString &chunk);
    void isProcessingChanged();
    void errorMessageChanged();
    void apiEndpointChanged();
    void apiKeyChanged();
    void modelNameChanged();
    void historyCleared();

private slots:
    void _onReplyFinished(QNetworkReply *reply);
    void _onReadyRead();

private:
    QString _collectVehicleContext();
    QString _collectParameterSummary();
    QJsonArray _buildMessagesArray(const QString &userMessage);
    void _setProcessing(bool processing);
    void _setError(const QString &error);
    void _parseStreamingResponse(const QByteArray &data);
    void _parseFinalResponse(const QByteArray &data);

    QNetworkAccessManager *_network = nullptr;
    QList<AgentMessage> _history;
    QString _apiEndpoint = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
    QString _apiKey;
    QString _modelName = "qwen-plus";
    bool _isProcessing = false;
    QString _errorMessage;
    QString _currentStreamContent;
    QNetworkReply *_currentReply = nullptr;

    static constexpr int kMaxHistoryMessages = 20;
    static constexpr int kRequestTimeoutMs = 30000;
};
