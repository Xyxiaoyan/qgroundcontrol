#include "AgentManager.h"
#include "LLMClient.h"
#include "ContextProvider.h"
#include "ConfigStore.h"
#include "ConversationHistory.h"
#include "LogAnalysisModel.h"
#include "LogAnalyzer.h"
#include "ToolRegistry.h"
#include "ReActLoop.h"
#include "Vehicle.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "AppSettings.h"
#include "SettingsManager.h"
#include "OnboardLogController.h"
#include "OnboardLogEntry.h"
#include "QmlObjectListModel.h"

#include "Tools/VehicleContextTool.h"
#include "Tools/FlightControlTool.h"
#include "Tools/RelativeMoveTool.h"
#include "Tools/AltitudeSpeedTool.h"
#include "Tools/FlightModeTool.h"
#include "Tools/ParameterTool.h"
#include "Tools/MavlinkCommandTool.h"
#include "Tools/LogAnalysisTool.h"
#include "Tools/LocationTool.h"
#include "Tools/MissionTool.h"
#include "Tools/RetrieveKnowledgeTool.h"
#include "Tools/RoutePlanningTool.h"
#include "Tools/POISearchTool.h"
#include "Tools/WeatherTool.h"
#include "Tools/DistanceTool.h"
#include "CloudRAGClient.h"
#include "ArchiveManager.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QDateTime>

QGC_LOGGING_CATEGORY(AgentManagerLog, "qgc.agent")

AgentManager *AgentManager::instance()  //单例模式，返回AgentManager的唯一实例
{
    static AgentManager *_instance = nullptr;
    if (!_instance) {
        _instance = new AgentManager();
    }
    return _instance;
}

AgentManager::AgentManager(QObject *parent)
    : QObject(parent)
    , _llm(new LLMClient(this))
    , _ctx(new ContextProvider(this))
    , _config(new ConfigStore(this))
    , _history(new ConversationHistory(this))
    , _logAnalyzer(new LogAnalyzer(this))
    , _toolRegistry(new ToolRegistry(this))
    , _ragClient(new CloudRAGClient(this))
{
    // Register all tools
    _toolRegistry->registerTool(new VehicleContextTool(_ctx, this));
    _toolRegistry->registerTool(new FlightControlTool(this));
    _toolRegistry->registerTool(new RelativeMoveTool(this));
    _toolRegistry->registerTool(new AltitudeSpeedTool(this));
    _toolRegistry->registerTool(new FlightModeTool(this));
    _toolRegistry->registerTool(new ParameterTool(ParameterTool::Mode::Get, this));
    _toolRegistry->registerTool(new ParameterTool(ParameterTool::Mode::Set, this));
    _toolRegistry->registerTool(new ParameterTool(ParameterTool::Mode::List, this));
    _toolRegistry->registerTool(new MavlinkCommandTool(this));
    _toolRegistry->registerTool(new LogAnalysisTool(_logAnalyzer, this));
    _toolRegistry->registerTool(new LocationTool(_ctx, this));
    _toolRegistry->registerTool(new MissionTool(this));
    _toolRegistry->registerTool(new RetrieveKnowledgeTool(_ragClient, this));
    _toolRegistry->registerTool(new RoutePlanningTool(_ctx, this));
    _toolRegistry->registerTool(new POISearchTool(_ctx, this));
    _toolRegistry->registerTool(new WeatherTool(_ctx, this));
    _toolRegistry->registerTool(new DistanceTool(_ctx, this));

    // Create ReAct loop
    _reactLoop = new ReActLoop(_llm, _toolRegistry, _ctx, this);
    _reactLoop->setConversationHistory(_history);
    _reactLoop->setRagClient(_ragClient);

    // Wire ReAct signals to AgentManager facade
    connect(_reactLoop, &ReActLoop::started,             this, &AgentManager::_onReActStarted);
    connect(_reactLoop, &ReActLoop::stopped,             this, &AgentManager::_onReActStopped);
    connect(_reactLoop, &ReActLoop::messageProduced,     this, &AgentManager::_onReActMessageProduced);
    connect(_reactLoop, &ReActLoop::streamingChunk,      this, &AgentManager::_onReActStreamChunk);
    connect(_reactLoop, &ReActLoop::thinkingChunk,       this, &AgentManager::_onReActThinkingChunk);
    connect(_reactLoop, &ReActLoop::toolConfirmationRequired, this, &AgentManager::_onReActToolConfirmationRequired);
    connect(_reactLoop, &ReActLoop::toolResult,          this, &AgentManager::_onReActToolResult);
    connect(_reactLoop, &ReActLoop::thoughtProduced,     this, &AgentManager::_onReActThought);
    connect(_reactLoop, &ReActLoop::toolInvoked,         this, &AgentManager::_onReActToolInvoked);
    connect(_reactLoop, &ReActLoop::finalAnswer,         this, &AgentManager::_onReActFinalAnswer);
    connect(_llm, &LLMClient::modelsFetched,              this, [this](const QJsonArray &models) {
        _connectionStatus = StatusConnected;
        emit connectionStatusChanged();
        emit modelsFetched(models);
    });
    connect(_llm, &LLMClient::requestError,              this, [this](const QString &err) {
        if (_connectionStatus == StatusConnecting) {
            _connectionStatus = StatusFailed;
            emit connectionStatusChanged();
        }
        _errorMessage = err;
        emit errorMessageChanged();
    });

    // Load persisted config (normalize endpoint on load)
    _apiKey = _config->apiKey();
    _modelName = _config->modelName();
    QString savedEndpoint = _config->apiEndpoint();
    if (!savedEndpoint.isEmpty()) {
        setApiEndpoint(savedEndpoint);
    }

    _reactLoop->setApiEndpoint(_apiEndpoint);
    _reactLoop->setApiKey(_apiKey);
    _reactLoop->setModelName(_modelName.isEmpty() ? QStringLiteral("deepseek-v4-flash") : _modelName);

    // Load geocode API key and forward to map provider
    _geocodeApiKey = _config->geocodeApiKey();
    if (!_geocodeApiKey.isEmpty()) {
        _ctx->setGeocodeApiKey(_geocodeApiKey);
    }

    // Load RAG configuration
    bool ragOn = _config->ragEnabled();
    QString ragUrl = _config->ragEndpoint();
    QString ragKey = _config->ragApiKey();
    // Default RAG API key to main API key if not separately configured
    if (ragKey.isEmpty()) {
        ragKey = _apiKey;
    }
    _ragClient->setEnabled(ragOn);
    _ragClient->setRagEndpoint(ragUrl);
    _ragClient->setApiKey(ragKey);

    connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged, this,
            [this](Vehicle *v) { emit vehicleConnectedChanged(v != nullptr); });
    if (MultiVehicleManager::instance()->activeVehicle()) {
        emit vehicleConnectedChanged(true);
    }
}

AgentManager::~AgentManager()
{
    cancelRequest();
}

void AgentManager::setApiKey(const QString &key)
{
    if (_apiKey != key) {
        _apiKey = key;
        _config->setApiKey(key);
        _reactLoop->setApiKey(key);
        emit apiKeyChanged();
    }
}

void AgentManager::setApiEndpoint(const QString &url)
{
    QString normalized = url.trimmed();
    if (normalized.isEmpty()) {
        if (_apiEndpoint != normalized) {
            _apiEndpoint = normalized;
            _config->setApiEndpoint(normalized);
            _reactLoop->setApiEndpoint(normalized);
            emit apiEndpointChanged();
        }
        return;
    }

    // Remove trailing slash
    while (normalized.endsWith(QStringLiteral("/"))) {
        normalized.chop(1);
    }

    // Already fully qualified — deduplicate and accept
    if (normalized.endsWith(QStringLiteral("/chat/completions"))) {
        // Remove any duplicate suffix (e.g. .../v1/chat/completions/v1/chat/completions)
        static const QStringList suffixes = {
            QStringLiteral("/v1/chat/completions"),
            QStringLiteral("/v4/chat/completions"),
        };
        for (const auto &suffix : suffixes) {
            QString dup = suffix + suffix;
            while (normalized.contains(dup)) {
                normalized.replace(dup, suffix);
            }
        }
        if (_apiEndpoint != normalized) {
            _apiEndpoint = normalized;
            _config->setApiEndpoint(normalized);
            _reactLoop->setApiEndpoint(normalized);
            emit apiEndpointChanged();
        }
        return;
    }

    // Base URL only — append /chat/completions
    // Detect versioned base paths: /v1, /v4, /compatible-mode/v1, etc.
    // Heuristic: if URL ends with /vN (N = digit), just append /chat/completions
    static const QRegularExpression versionedRe(QStringLiteral("/v\\d+$"));
    if (versionedRe.match(normalized).hasMatch()) {
        normalized += QStringLiteral("/chat/completions");
    } else {
        // Default: append /v1/chat/completions
        normalized += QStringLiteral("/v1/chat/completions");
    }

    if (_apiEndpoint != normalized) {
        _apiEndpoint = normalized;
        _config->setApiEndpoint(normalized);
        _reactLoop->setApiEndpoint(normalized);
        emit apiEndpointChanged();
    }
}

QString AgentManager::baseApiEndpoint() const
{
    QString base = _apiEndpoint;
    // Strip /chat/completions suffix (handles /v1, /v4, etc.)
    if (base.endsWith(QStringLiteral("/chat/completions"))) {
        base.chop(18);
    }
    return base;
}

void AgentManager::setModelName(const QString &model)
{
    if (_modelName != model) {
        _modelName = model;
        _config->setModelName(model);
        _reactLoop->setModelName(model);
        emit modelNameChanged();
    }
}

void AgentManager::setGeocodeApiKey(const QString &key)
{
    if (_geocodeApiKey != key) {
        _geocodeApiKey = key;
        _config->setGeocodeApiKey(key);
        _ctx->setGeocodeApiKey(key);
        emit geocodeApiKeyChanged();
    }
}

bool AgentManager::ragEnabled() const
{
    return _ragClient ? _ragClient->isEnabled() : false;
}

QString AgentManager::ragEndpoint() const
{
    return _ragClient ? _ragClient->ragEndpoint() : QString();
}

QString AgentManager::ragApiKey() const
{
    return _ragClient ? _ragClient->apiKey() : QString();
}

void AgentManager::setRagEnabled(bool enabled)
{
    if (_ragClient) {
        _ragClient->setEnabled(enabled);
        _config->setRagEnabled(enabled);
        emit ragEnabledChanged();
    }
}

void AgentManager::setRagEndpoint(const QString &url)
{
    if (_ragClient) {
        _ragClient->setRagEndpoint(url);
        _config->setRagEndpoint(url);
        emit ragEndpointChanged();
    }
}

void AgentManager::setRagApiKey(const QString &key)
{
    if (_ragClient) {
        _ragClient->setApiKey(key);
        _config->setRagApiKey(key);
        emit ragApiKeyChanged();
    }
}

void AgentManager::testRagConnection()
{
    if (!_ragClient || !_ragClient->isEnabled()) {
        emit ragTestResult(QStringLiteral("RAG is not enabled"));
        return;
    }
    if (_ragClient->ragEndpoint().isEmpty()) {
        emit ragTestResult(QStringLiteral("RAG endpoint is not configured"));
        return;
    }

    auto results = _ragClient->retrieveSync(QStringLiteral("drone"), 1, 0.0);
    if (!results.isEmpty()) {
        emit ragTestResult(QStringLiteral("✅ Connection successful, retrieved %1 result(s)").arg(results.size()));
    } else {
        emit ragTestResult(QStringLiteral("❌ No results retrieved, please check endpoint and knowledge base configuration"));
    }
}

void AgentManager::sendMessage(const QString &userMessage)   //转发用户消息到ReActLoop
{
    if (_isProcessing || userMessage.trimmed().isEmpty()) return;
    if (_apiKey.isEmpty()) {
        _errorMessage = QStringLiteral("Please set API Key");
        emit errorMessageChanged();
        return;
    }
    _errorMessage.clear();
    emit errorMessageChanged();
    _streamAccum.clear();
    emit streamingTextChanged();

    emit messageReceived(QStringLiteral("user"), userMessage); //将用户消息发送到界面

    _reactLoop->start(userMessage, false); //启动ReActLoop，传入用户消息和是否自主模式（false表示非自主模式）
}

void AgentManager::cancelRequest()
{
    _reactLoop->cancel();
}

void AgentManager::confirmCommand(const QVariantMap &cmdMap)
{
    Q_UNUSED(cmdMap);
    _reactLoop->confirmToolExecution();
}

void AgentManager::rejectCommand()
{
    _reactLoop->rejectToolExecution();
}

void AgentManager::runQuickCheck(const QString &checkType)
{
    static const QHash<QString, QString> prompts = {
        {QStringLiteral("battery"),    QStringLiteral("Check battery status, including voltage, charge level, temperature, and current. Warn clearly if any abnormality is detected.")},
        {QStringLiteral("gps"),        QStringLiteral("Check GPS status: satellite count, HDOP, fix type. Assess whether conditions are safe for flight.")},
        {QStringLiteral("vibration"),  QStringLiteral("Analyze current vibration levels. If vibration is too high, provide troubleshooting recommendations.")},
        {QStringLiteral("preflight"),  QStringLiteral("Perform a complete pre-flight check: battery, GPS, sensors, RC link, propellers, and safety switch.")},
        {QStringLiteral("params"),     QStringLiteral("Check flight controller parameters for anomalies: PID, calibration, and safety switch configuration.")}
    };
    sendMessage(prompts.value(checkType, checkType));
}

void AgentManager::sendVisionQuery(const QString &prompt, const QString &imageBase64)
{
    if (_isProcessing || prompt.trimmed().isEmpty()) return;
    if (_apiKey.isEmpty()) {
        _errorMessage = QStringLiteral("Please set API Key");
        emit errorMessageChanged();
        return;
    }
    if (imageBase64.isEmpty()) {
        _errorMessage = QStringLiteral("Image data is empty");
        emit errorMessageChanged();
        return;
    }

    _errorMessage.clear();
    emit errorMessageChanged();
    _isProcessing = true;
    emit isProcessingChanged();
    _statusText = QStringLiteral("Analyzing vision...");
    emit statusTextChanged();

    emit messageReceived(QStringLiteral("user"), QStringLiteral("[Screenshot] ") + prompt);
    _chatMessages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("[Screenshot] ") + prompt}});

    QString enrichedPrompt = prompt + QStringLiteral("\n\nCurrent Vehicle Status:\n") + _ctx->buildVehicleContext();

    _inVisionQuery = true;
    connect(_llm, &LLMClient::responseReceived, this, &AgentManager::_onVisionResponse, Qt::SingleShotConnection);
    connect(_llm, &LLMClient::requestError, this, &AgentManager::_onVisionError, Qt::SingleShotConnection);
    _llm->sendVisionRequest(_apiEndpoint, _apiKey, _modelName, enrichedPrompt, imageBase64);
}

void AgentManager::startAutonomousTask(const QString &task)
{
    if (_isProcessing) return;
    if (_apiKey.isEmpty()) {
        _errorMessage = QStringLiteral("Please set API Key");
        emit errorMessageChanged();
        return;
    }
    if (_totalTokens >= kDailyTokenLimit) {
        _errorMessage = QStringLiteral("Daily token limit reached (%1), please try again tomorrow").arg(kDailyTokenLimit);
        emit errorMessageChanged();
        return;
    }

    _isAutonomous = true;
    emit isAutonomousChanged();
    _streamAccum.clear();
    emit streamingTextChanged();
    _errorMessage.clear();
    emit errorMessageChanged();

    _statusText = QStringLiteral("Autonomous mode started");
    emit statusTextChanged();
    emit messageReceived(QStringLiteral("system"), QStringLiteral("Autonomous task: ") + task);

    _reactLoop->start(task, true);
}

void AgentManager::stopAutonomousTask()
{
    if (!_isAutonomous) return;
    _reactLoop->cancel();
}

void AgentManager::setAutonomous(bool autonomous)
{
    if (_isAutonomous == autonomous) return;
    _isAutonomous = autonomous;
    // Sync to ReActLoop so the next tool call uses the right mode
    _reactLoop->setAutonomous(autonomous);
    emit isAutonomousChanged();

    _statusText = autonomous ? QStringLiteral("Switched to autonomous mode") : QStringLiteral("Switched to manual mode");
    emit statusTextChanged();

    // If we just switched to autonomous while awaiting confirmation, auto-confirm
    if (autonomous && _reactLoop->isAwaitingConfirmation()) {
        _reactLoop->confirmToolExecution();
    }
}

void AgentManager::fetchModels()
{
    if (_apiEndpoint.isEmpty() || _apiKey.isEmpty()) {
        _errorMessage = QStringLiteral("Please set Base URL and API Key first");
        emit errorMessageChanged();
        return;
    }
    _statusText = QStringLiteral("Connecting...");
    emit statusTextChanged();
    _connectionStatus = StatusConnecting;
    emit connectionStatusChanged();
    _llm->fetchModels(_apiEndpoint, _apiKey);
}

void AgentManager::testConnection()
{
    fetchModels();
}

void AgentManager::clearConversationHistory()
{
    _reactLoop->clearHistory();
    emit messageReceived(QStringLiteral("system"), QStringLiteral("Chat history cleared"));
}

bool AgentManager::hasConversationHistory() const
{
    return _reactLoop->hasHistory();
}

// ═══ Chat Archive ═══

QString AgentManager::archiveCurrentChat()
{
    if (_chatMessages.isEmpty()) return {};

    QString id = _archiveManager->archiveCurrent(_chatMessages);
    if (!id.isEmpty()) {
        emit messageReceived(QStringLiteral("system"),
            QStringLiteral("Chat archived (%1 messages)").arg(_chatMessages.size()));
        emit archiveListChanged();
    }
    return id;
}

QVariantList AgentManager::listArchives() const
{
    return _archiveManager->listArchives();
}

QVariantList AgentManager::loadArchiveMessages(const QString &id) const
{
    QJsonArray messages = _archiveManager->loadArchive(id);
    QVariantList result;
    for (const auto &msgVal : messages) {
        result.append(msgVal.toVariant().toMap());
    }
    return result;
}

bool AgentManager::deleteArchive(const QString &id)
{
    bool ok = _archiveManager->deleteArchive(id);
    if (ok) emit archiveListChanged();
    return ok;
}

int AgentManager::archiveCount() const
{
    return _archiveManager->archiveCount();
}

void AgentManager::runLogAnalysis(const QString &filePath, const QString &analysisType)
{
    if (_isProcessing) return;
    if (_apiKey.isEmpty()) {
        _errorMessage = QStringLiteral("Please set API Key first");
        emit errorMessageChanged();
        return;
    }

    if (!_logAnalyzer->loadLog(filePath)) {
        _errorMessage = _logAnalyzer->errorString();
        emit errorMessageChanged();
        emit messageReceived(QStringLiteral("system"), QStringLiteral("✗ ") + _errorMessage);
        return;
    }

    _statusText = QStringLiteral("Analyzing log...");
    emit statusTextChanged();
    emit messageReceived(QStringLiteral("system"),
        QStringLiteral("Starting analysis: ") + filePath + QStringLiteral(" (") + analysisType + QStringLiteral(")"));

    QJsonObject results = _logAnalyzer->analyze(analysisType);
    QString report = _logAnalyzer->generateReport(results);

    // Populate chart data model
    if (!_logAnalysisModel) {
        _logAnalysisModel = new LogAnalysisModel(this);
        emit logAnalysisModelChanged();
    }
    auto ts = _logAnalyzer->timeSeriesJson();
    _logAnalysisModel->setData(
        results,
        ts.flightModeSeries,
        ts.accelSeries,
        ts.gyroSeries,
        ts.gpsSeries,
        ts.batterySeries,
        ts.ekfSeries,
        ts.pidSeries,
        ts.opticalFlowSeries
    );

    _statusText = QStringLiteral("Ready");
    emit statusTextChanged();

    emit logAnalysisReady(report);
    sendMessage(report);
}

void AgentManager::listDownloadedLogs()
{
    const QString logDir = SettingsManager::instance()->appSettings()->logSavePath();
    _downloadedLogsCache = {};

    QDir dir(logDir);
    if (!dir.exists()) {
        emit messageReceived(QStringLiteral("system"), QStringLiteral("Log directory does not exist: ") + logDir);
        return;
    }

    QStringList filters = {QStringLiteral("*.ulg"), QStringLiteral("*.bin"), QStringLiteral("*.px4log")};
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Time);

    if (files.isEmpty()) {
        emit messageReceived(QStringLiteral("system"),
            QStringLiteral("Log directory is empty: ") + logDir + QStringLiteral("\nPlease download onboard logs first via Analyze → Onboard Logs"));
        return;
    }

    QString msg = QStringLiteral("Downloaded logs (%1):\n").arg(files.size());
    msg += QString(30, QChar(0x2500)) + QStringLiteral("\n");

    QJsonArray arr;
    for (int i = 0; i < std::min(15, static_cast<int>(files.size())); i++) {
        const QFileInfo &fi = files[i];
        QString sizeStr;
        qint64 sz = fi.size();
        if (sz < 1024) sizeStr = QString::number(sz) + QStringLiteral("B");
        else if (sz < 1024 * 1024) sizeStr = QString::number(sz / 1024.0, 'f', 1) + QStringLiteral("KB");
        else sizeStr = QString::number(sz / (1024.0 * 1024.0), 'f', 1) + QStringLiteral("MB");

        QJsonObject entry;
        entry[QStringLiteral("name")] = fi.fileName();
        entry[QStringLiteral("path")] = fi.absoluteFilePath();
        entry[QStringLiteral("size")] = sizeStr;
        entry[QStringLiteral("date")] = fi.lastModified().toString(QStringLiteral("MM-dd hh:mm"));
        entry[QStringLiteral("index")] = i;
        arr.append(entry);

        msg += QStringLiteral("%1. %2 (%3, %4)\n").arg(i + 1).arg(fi.fileName()).arg(sizeStr).arg(entry[QStringLiteral("date")].toString());
    }
    if (files.size() > 15) {
        msg += QStringLiteral("... and %1 more files\n").arg(files.size() - 15);
    }
    msg += QString(30, QChar(0x2500)) + QStringLiteral("\n");
    msg += QStringLiteral("Click \"Analyze #\" to select by index, or \"Analyze Latest\" to auto-analyze the most recent log");

    _downloadedLogsCache = arr;
    emit downloadedLogsList(arr);
    emit messageReceived(QStringLiteral("system"), msg);
}

void AgentManager::analyzeLogByIndex(int index)
{
    if (index < 0 || index >= _downloadedLogsCache.size()) {
        emit messageReceived(QStringLiteral("system"), QStringLiteral("Invalid log index: ") + QString::number(index + 1));
        return;
    }
    QJsonObject entry = _downloadedLogsCache[index].toObject();
    QString path = entry[QStringLiteral("path")].toString();
    runLogAnalysis(path, QStringLiteral("all"));
}

void AgentManager::downloadOnboardLogs()
{
    if (!MultiVehicleManager::instance()->activeVehicle()) {
        emit messageReceived(QStringLiteral("system"), QStringLiteral("No vehicle connected, cannot download onboard logs"));
        return;
    }

    if (!_onboardLogCtrl) {
        _onboardLogCtrl = new OnboardLogController(this);

        connect(_onboardLogCtrl, &OnboardLogController::requestingListChanged, this,
                &AgentManager::_onOnboardLogListComplete);

        connect(_onboardLogCtrl, &OnboardLogController::downloadingLogsChanged, this,
                &AgentManager::_onOnboardLogDownloadComplete);
    }

    _statusText = QStringLiteral("Requesting onboard log list...");
    emit statusTextChanged();
    emit messageReceived(QStringLiteral("system"), QStringLiteral("Requesting onboard log list..."));

    _onboardDownloadDir = SettingsManager::instance()->appSettings()->logSavePath();
    _onboardLogCtrl->refresh();
}

QJsonArray AgentManager::listOnboardLogs()
{
    QJsonArray arr;
    auto *model = _onboardLogCtrl
        ? qobject_cast<QmlObjectListModel *>(_onboardLogCtrl->property("model").value<QObject *>())
        : nullptr;
    if (!model) return arr;

    for (int i = 0; i < model->count(); i++) {
        auto *entry = model->value<QGCOnboardLogEntry *>(i);
        if (entry) {
            QJsonObject o;
            o["index"] = i;
            o["id"] = static_cast<int>(entry->id());
            o["size"] = entry->size() < 1024*1024
                ? QString::number(entry->size() / 1024.0, 'f', 1) + "KB"
                : QString::number(entry->size() / (1024.0*1024.0), 'f', 1) + "MB";
            o["time"] = entry->time().toString("yyyy-MM-dd hh:mm:ss");
            o["selected"] = entry->selected();
            o["status"] = entry->status();
            arr.append(o);
        }
    }
    return arr;
}

void AgentManager::downloadOnboardLogByIndex(int index)
{
    if (!_onboardLogCtrl) {
        emit messageReceived(QStringLiteral("system"), QStringLiteral("Please request onboard log list first"));
        return;
    }
    auto *model = qobject_cast<QmlObjectListModel *>(
        _onboardLogCtrl->property("model").value<QObject *>());
    if (!model || index < 0 || index >= model->count()) {
        emit messageReceived(QStringLiteral("system"), QStringLiteral("Invalid log index"));
        return;
    }
    auto *entry = model->value<QGCOnboardLogEntry *>(index);
    if (!entry) {
        emit messageReceived(QStringLiteral("system"), QStringLiteral("Invalid log entry"));
        return;
    }
    entry->setSelected(true);
    QString logInfo = QStringLiteral("Downloading #%1 (%2)")
        .arg(entry->id())
        .arg(entry->size() < 1024 * 1024
            ? QString::number(entry->size() / 1024.0, 'f', 1) + QStringLiteral("KB")
            : QString::number(entry->size() / (1024.0 * 1024.0), 'f', 1) + QStringLiteral("MB"));
    emit messageReceived(QStringLiteral("system"), logInfo);
    emit onboardDownloadProgress(logInfo);
    _statusText = QStringLiteral("Downloading onboard log...");
    emit statusTextChanged();
    _onboardLogCtrl->download(_onboardDownloadDir);
}

// ---- ReAct signal forwarding ----

void AgentManager::_onReActStarted()
{
    _isProcessing = true;
    emit isProcessingChanged();
    _streamAccum.clear();
    if (_streamThrottle) _streamThrottle->stop();
    emit streamingTextChanged();
    _thinkingAccum.clear();
    if (_thinkThrottle) _thinkThrottle->stop();
    emit thinkingTextChanged();
    _lastThought.clear();
    _lastToolAction.clear();
}

void AgentManager::_onReActStopped()
{
    // Flush throttled stream before marking idle
    if (_streamThrottle && _streamThrottle->isActive()) {
        _streamThrottle->stop();
        emit streamingTextChanged();
    }

    // Flush throttled thinking before marking idle
    if (_thinkThrottle && _thinkThrottle->isActive()) {
        _thinkThrottle->stop();
        emit thinkingTextChanged();
    }

    // If streaming text accumulated but no final messageProduced was emitted
    // (e.g. thinking-mode models where contentBuffer is empty), flush the
    // stream as the final assistant answer so QML has something to display.
    if (!_streamAccum.isEmpty()) {
        emit messageReceived(QStringLiteral("assistant"), _streamAccum);
        _streamAccum.clear();
        emit streamingTextChanged();
    }

    _isProcessing = false;
    emit isProcessingChanged();
    _statusText = _isAutonomous ? QStringLiteral("Autonomous mode standing by") : QStringLiteral("Ready");
    emit statusTextChanged();
}

void AgentManager::_onReActMessageProduced(const QString &role, const QString &content)
{
    _trackTokens(content);
    emit messageReceived(role, content);
}

void AgentManager::_onReActStreamChunk(const QString &text)
{
    _streamAccum += text;

    // Throttle: emit at most once per 150ms to prevent QML layout thrashing
    if (!_streamThrottle) {
        _streamThrottle = new QTimer(this);
        _streamThrottle->setSingleShot(true);
        _streamThrottle->setInterval(150);
        connect(_streamThrottle, &QTimer::timeout, this, [this]() {
            emit streamingTextChanged();
        });
    }
    _streamThrottle->start(); // Restart — only fires if idle for 150ms
}

void AgentManager::_onReActThinkingChunk(const QString &text)
{
    _thinkingAccum += text;

    // Throttle: emit at most once per 200ms to prevent QML layout thrashing
    if (!_thinkThrottle) {
        _thinkThrottle = new QTimer(this);
        _thinkThrottle->setSingleShot(true);
        _thinkThrottle->setInterval(200);
        connect(_thinkThrottle, &QTimer::timeout, this, [this]() {
            emit thinkingTextChanged();
        });
    }
    _thinkThrottle->start(); // Restart — only fires if idle for 200ms
}

void AgentManager::_onReActToolConfirmationRequired(const QString &toolName, const QString &description, const QJsonObject &parameters)
{
    Q_UNUSED(description);

    // Build human-readable command description from tool + params
    QString cmdDesc = _buildCommandDescription(toolName, parameters);

    QVariantMap cmdMap;
    cmdMap[QStringLiteral("toolName")] = toolName;
    cmdMap[QStringLiteral("description")] = cmdDesc;
    cmdMap[QStringLiteral("parameters")] = parameters.toVariantMap();

    _statusText = QStringLiteral("Awaiting confirmation: %1").arg(cmdDesc);
    emit statusTextChanged();
    emit commandPending(cmdDesc, cmdMap);
}

void AgentManager::_onReActToolResult(const QString &toolName, const QJsonObject &observation)
{
    if (observation.contains(QStringLiteral("error"))) {
        emit commandFailed(observation[QStringLiteral("error")].toString());
    } else {
        QString result = observation.contains(QStringLiteral("result"))
            ? observation[QStringLiteral("result")].toString()
            : QStringLiteral("%1 complete").arg(toolName);
        emit commandExecuted(result);
    }
}

void AgentManager::_onReActThought(const QString &thought)
{
    _lastThought = thought;
    if (_isAutonomous) {
        emit autonomousStep(thought, QString());
    } else {
        _statusText = QStringLiteral("Thinking: %1").arg(thought.left(80));
        emit statusTextChanged();
    }
}

void AgentManager::_onReActToolInvoked(const QString &toolName, const QJsonObject &parameters)
{
    _lastToolAction = toolName;
    QString actionDesc = _buildCommandDescription(toolName, parameters);
    if (_isAutonomous) {
        emit autonomousStep(_lastThought.isEmpty() ? actionDesc : _lastThought, actionDesc);
    } else {
        _statusText = QStringLiteral("Executing: %1").arg(actionDesc);
        emit statusTextChanged();
    }
}

void AgentManager::_onReActFinalAnswer(const QString &answer)
{
    if (_isAutonomous) {
        emit autonomousStep(answer, QStringLiteral("[Task complete]"));
    }
    // In manual mode, the answer is already displayed via messageReceived from _onReActMessageProduced
}

// ---- Vision query handlers ----

void AgentManager::_onVisionResponse(const QString &content)
{
    _inVisionQuery = false;
    _isProcessing = false;
    emit isProcessingChanged();

    _trackTokens(content);
    _statusText = QStringLiteral("Ready");
    emit statusTextChanged();

    _chatMessages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), content}});
    emit messageReceived(QStringLiteral("assistant"), content);
}

void AgentManager::_onVisionError(const QString &error)
{
    _inVisionQuery = false;
    _isProcessing = false;
    emit isProcessingChanged();

    _errorMessage = error;
    emit errorMessageChanged();
    _statusText = QStringLiteral("Error");
    emit statusTextChanged();
    emit messageReceived(QStringLiteral("system"), QStringLiteral("Vision analysis failed: ") + error);
}

// ---- Onboard log slots ----

void AgentManager::_onOnboardLogListComplete()
{
    if (_onboardLogCtrl->property("requestingList").toBool()) return;

    auto *model = qobject_cast<QmlObjectListModel *>(
        _onboardLogCtrl->property("model").value<QObject *>());
    if (!model || model->count() == 0) {
        emit messageReceived(QStringLiteral("system"), QStringLiteral("No onboard logs found"));
        _statusText = QStringLiteral("Ready");
        emit statusTextChanged();
        return;
    }

    // Build list message for UI
    QStringList lines;
    lines.append(QStringLiteral("Onboard log list (%1):").arg(model->count()));
    for (int i = 0; i < model->count(); i++) {
        auto *e = model->value<QGCOnboardLogEntry *>(i);
        if (e) {
            QString sizeStr = e->size() < 1024 * 1024
                ? QString::number(e->size() / 1024.0, 'f', 1) + "KB"
                : QString::number(e->size() / (1024.0 * 1024.0), 'f', 1) + "MB";
            lines.append(QStringLiteral("  [#%1] ID:%2 %3 %4")
                .arg(i).arg(e->id()).arg(e->time().toString("MM-dd hh:mm")).arg(sizeStr));
        }
    }
    lines.append(QStringLiteral("---"));
    lines.append(QStringLiteral("Please select a log to download from the list"));

    emit messageReceived(QStringLiteral("system"), lines.join("\n"));
    emit onboardDownloadProgress(QStringLiteral("List loaded"));
    _statusText = QStringLiteral("Ready");
    emit statusTextChanged();
}

void AgentManager::_onOnboardLogDownloadComplete()
{
    if (_onboardLogCtrl->property("downloadingLogs").toBool()) return;

    QDir dir(_onboardDownloadDir);
    QStringList filters = {QStringLiteral("*.ulg"), QStringLiteral("*.bin"), QStringLiteral("*.px4log")};
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Time);

    if (files.isEmpty()) {
        emit messageReceived(QStringLiteral("system"), QStringLiteral("Download completed but log file not found"));
        _statusText = QStringLiteral("Ready");
        emit statusTextChanged();
        return;
    }

    QString path = files.first().absoluteFilePath();
    QString name = files.first().fileName();
    emit messageReceived(QStringLiteral("system"),
        QStringLiteral("Download complete: ") + name + QStringLiteral("\nStarting auto analysis..."));
    emit onboardDownloadComplete(path);
    _statusText = QStringLiteral("Ready");
    emit statusTextChanged();

    runLogAnalysis(path, QStringLiteral("all"));
}

QString AgentManager::_buildCommandDescription(const QString &toolName, const QJsonObject &p) const
{
    if (toolName == QStringLiteral("flight_control")) {
        QString action = p[QStringLiteral("action")].toString();
        if (action == QStringLiteral("takeoff")) {
            double alt = p[QStringLiteral("altitude")].toDouble(10);
            return QStringLiteral("Take off to %1m").arg(alt);
        }
        if (action == QStringLiteral("land"))  return QStringLiteral("Land");
        if (action == QStringLiteral("rtl"))   return QStringLiteral("Return to home (RTL)");
        if (action == QStringLiteral("pause")) return QStringLiteral("Hover");
        if (action == QStringLiteral("goto")) {
            double lat = p[QStringLiteral("latitude")].toDouble();
            double lon = p[QStringLiteral("longitude")].toDouble();
            double alt = p[QStringLiteral("altitude")].toDouble(50);
            return QStringLiteral("Goto (%1, %2) alt %3m").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(alt);
        }
        if (action == QStringLiteral("orbit")) {
            double lat = p[QStringLiteral("latitude")].toDouble();
            double lon = p[QStringLiteral("longitude")].toDouble();
            double r = p[QStringLiteral("radius")].toDouble(30);
            return QStringLiteral("Orbit (%1, %2) radius %3m").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(r);
        }
        return QStringLiteral("Flight control: %1").arg(action);
    }
    if (toolName == QStringLiteral("relative_move")) {
        QString dir = p[QStringLiteral("direction")].toString();
        double m = p[QStringLiteral("meters")].toDouble(5);
        if (dir == QStringLiteral("fwd"))  return QStringLiteral("Forward %1m").arg(m);
        if (dir == QStringLiteral("back")) return QStringLiteral("Backward %1m").arg(m);
        if (dir == QStringLiteral("left")) return QStringLiteral("Left %1m").arg(m);
        if (dir == QStringLiteral("right")) return QStringLiteral("Right %1m").arg(m);
        if (dir == QStringLiteral("turn")) return QStringLiteral("Turn %1deg").arg(m);
        return QStringLiteral("Relative move: %1 %2m").arg(dir).arg(m);
    }
    if (toolName == QStringLiteral("altitude_speed")) {
        QString param = p[QStringLiteral("parameter")].toString();
        double val = p[QStringLiteral("value")].toDouble();
        if (param == QStringLiteral("altitude")) return QStringLiteral("Altitude change %1m").arg(val);
        if (param == QStringLiteral("speed"))    return QStringLiteral("Ground speed set to %1m/s").arg(val);
        return QStringLiteral("alt/speed: %1 = %2").arg(param).arg(val);
    }
    if (toolName == QStringLiteral("flight_mode")) {
        QString mode = p[QStringLiteral("mode")].toString();
        return QStringLiteral("Switch mode: %1").arg(mode);
    }
    if (toolName == QStringLiteral("get_parameter")) {
        return QStringLiteral("Query parameter: %1").arg(p[QStringLiteral("name")].toString());
    }
    if (toolName == QStringLiteral("set_parameter")) {
        return QStringLiteral("Set %1 = %2").arg(p[QStringLiteral("name")].toString()).arg(p[QStringLiteral("value")].toDouble());
    }
    if (toolName == QStringLiteral("list_parameters")) {
        return QStringLiteral("List parameters: %1").arg(p[QStringLiteral("keyword")].toString());
    }
    if (toolName == QStringLiteral("mavlink_command")) {
        QString cmd = p[QStringLiteral("command_name")].toString();
        return QStringLiteral("MAVLink: %1").arg(cmd);
    }
    if (toolName == QStringLiteral("log_analysis")) {
        return QStringLiteral("Log analysis: %1").arg(p[QStringLiteral("analysis_type")].toString(QStringLiteral("all")));
    }
    if (toolName == QStringLiteral("mission")) {
        QString action = p[QStringLiteral("action")].toString();
        if (action == QStringLiteral("add_takeoff")) {
            double alt = p[QStringLiteral("altitude")].toDouble(10);
            return QStringLiteral("Set takeoff altitude %1m").arg(alt);
        }
        if (action == QStringLiteral("add_waypoint")) {
            double lat = p[QStringLiteral("latitude")].toDouble();
            double lon = p[QStringLiteral("longitude")].toDouble();
            double alt = p[QStringLiteral("altitude")].toDouble(50);
            return QStringLiteral("Add waypoint (%1, %2, %3m)").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(alt);
        }
        if (action == QStringLiteral("remove_waypoint")) {
            return QStringLiteral("Remove waypoint #%1").arg(p[QStringLiteral("index")].toInt());
        }
        if (action == QStringLiteral("clear"))  return QStringLiteral("Clear waypoints");
        if (action == QStringLiteral("list"))   return QStringLiteral("List waypoints");
        if (action == QStringLiteral("upload"))  return QStringLiteral("Upload mission");
        if (action == QStringLiteral("download")) return QStringLiteral("Download mission");
        return QStringLiteral("Mission: %1").arg(action);
    }
    // Fallback: tool name + first param values
    QString desc = toolName;
    QStringList vals;
    for (auto it = p.begin(); it != p.end(); ++it) {
        if (it.value().isDouble() || it.value().isString()) {
            vals.append(it.value().toVariant().toString());
        }
    }
    if (!vals.isEmpty()) desc += QStringLiteral(" ") + vals.join(QStringLiteral(" "));
    return desc;
}

void AgentManager::_trackTokens(const QString &text)
{
    int chars = text.size();
    int tokens = chars / 3;
    _totalTokens += tokens;
    emit totalTokensChanged();
}
