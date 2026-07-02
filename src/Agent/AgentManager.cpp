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

    // Create ReAct loop
    _reactLoop = new ReActLoop(_llm, _toolRegistry, _ctx, this);
    _reactLoop->setConversationHistory(_history);

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
    connect(_llm, &LLMClient::modelsFetched,              this, &AgentManager::modelsFetched);
    connect(_llm, &LLMClient::requestError,              this, [this](const QString &err) {
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

void AgentManager::sendMessage(const QString &userMessage)   //转发用户消息到ReActLoop
{
    if (_isProcessing || userMessage.trimmed().isEmpty()) return;
    if (_apiKey.isEmpty()) {
        _errorMessage = QStringLiteral("请设置 API Key");
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
        {QStringLiteral("battery"),    QStringLiteral("检查电池状态，包括电压、电量、温度、电流。有异常明确警告。")},
        {QStringLiteral("gps"),        QStringLiteral("检查GPS状态，卫星数、HDOP、定位类型，判断是否满足安全飞行。")},
        {QStringLiteral("vibration"),  QStringLiteral("分析当前震动水平，震动过高给出排查建议。")},
        {QStringLiteral("preflight"),  QStringLiteral("执行完整飞前检查：电池、GPS、传感器、遥控器、螺旋桨、安全开关。")},
        {QStringLiteral("params"),     QStringLiteral("检查飞控参数异常：PID、校准、安全开关配置。")}
    };
    sendMessage(prompts.value(checkType, checkType));
}

void AgentManager::sendVisionQuery(const QString &prompt, const QString &imageBase64)
{
    if (_isProcessing || prompt.trimmed().isEmpty()) return;
    if (_apiKey.isEmpty()) {
        _errorMessage = QStringLiteral("请设置 API Key");
        emit errorMessageChanged();
        return;
    }
    if (imageBase64.isEmpty()) {
        _errorMessage = QStringLiteral("图像数据为空");
        emit errorMessageChanged();
        return;
    }

    _errorMessage.clear();
    emit errorMessageChanged();
    _isProcessing = true;
    emit isProcessingChanged();
    _statusText = QStringLiteral("视觉分析中...");
    emit statusTextChanged();

    emit messageReceived(QStringLiteral("user"), QStringLiteral("[截图] ") + prompt);
    _chatMessages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("[截图] ") + prompt}});

    QString enrichedPrompt = prompt + QStringLiteral("\n\n当前飞行器状态:\n") + _ctx->buildVehicleContext();

    _inVisionQuery = true;
    connect(_llm, &LLMClient::responseReceived, this, &AgentManager::_onVisionResponse, Qt::SingleShotConnection);
    connect(_llm, &LLMClient::requestError, this, &AgentManager::_onVisionError, Qt::SingleShotConnection);
    _llm->sendVisionRequest(_apiEndpoint, _apiKey, _modelName, enrichedPrompt, imageBase64);
}

void AgentManager::startAutonomousTask(const QString &task)
{
    if (_isProcessing) return;
    if (_apiKey.isEmpty()) {
        _errorMessage = QStringLiteral("请设置 API Key");
        emit errorMessageChanged();
        return;
    }
    if (_totalTokens >= kDailyTokenLimit) {
        _errorMessage = QStringLiteral("已达每日 Token 限额 (%1)，请明天再试").arg(kDailyTokenLimit);
        emit errorMessageChanged();
        return;
    }

    _isAutonomous = true;
    emit isAutonomousChanged();
    _streamAccum.clear();
    emit streamingTextChanged();
    _errorMessage.clear();
    emit errorMessageChanged();

    _statusText = QStringLiteral("自主模式启动");
    emit statusTextChanged();
    emit messageReceived(QStringLiteral("system"), QStringLiteral("🚀 自主任务: ") + task);

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

    _statusText = autonomous ? QStringLiteral("已切换为自主模式") : QStringLiteral("已切换为手动模式");
    emit statusTextChanged();

    // If we just switched to autonomous while awaiting confirmation, auto-confirm
    if (autonomous && _reactLoop->isAwaitingConfirmation()) {
        _reactLoop->confirmToolExecution();
    }
}

void AgentManager::fetchModels()
{
    if (_apiEndpoint.isEmpty() || _apiKey.isEmpty()) {
        _errorMessage = QStringLiteral("请先设置 API Endpoint 和 API Key");
        emit errorMessageChanged();
        return;
    }
    _statusText = QStringLiteral("获取模型列表...");
    emit statusTextChanged();
    _llm->fetchModels(_apiEndpoint, _apiKey);
}

void AgentManager::clearConversationHistory()
{
    _reactLoop->clearHistory();
    emit messageReceived(QStringLiteral("system"), QStringLiteral("🗑 对话历史已清除"));
}

bool AgentManager::hasConversationHistory() const
{
    return _reactLoop->hasHistory();
}

void AgentManager::runLogAnalysis(const QString &filePath, const QString &analysisType)
{
    if (_isProcessing) return;
    if (_apiKey.isEmpty()) {
        _errorMessage = QStringLiteral("请先设置 API Key");
        emit errorMessageChanged();
        return;
    }

    if (!_logAnalyzer->loadLog(filePath)) {
        _errorMessage = _logAnalyzer->errorString();
        emit errorMessageChanged();
        emit messageReceived(QStringLiteral("system"), QStringLiteral("✗ ") + _errorMessage);
        return;
    }

    _statusText = QStringLiteral("分析日志中...");
    emit statusTextChanged();
    emit messageReceived(QStringLiteral("system"),
        QStringLiteral("开始分析: ") + filePath + QStringLiteral(" (") + analysisType + QStringLiteral(")"));

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

    _statusText = QStringLiteral("就绪");
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
        emit messageReceived(QStringLiteral("system"), QStringLiteral("日志目录不存在: ") + logDir);
        return;
    }

    QStringList filters = {QStringLiteral("*.ulg"), QStringLiteral("*.bin"), QStringLiteral("*.px4log")};
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Time);

    if (files.isEmpty()) {
        emit messageReceived(QStringLiteral("system"),
            QStringLiteral("日志目录为空: ") + logDir + QStringLiteral("\n请先在 Analyze → Onboard Logs 中下载机载日志"));
        return;
    }

    QString msg = QStringLiteral("📂 已下载的日志 (%1 个):\n").arg(files.size());
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
        msg += QStringLiteral("… 还有 %1 个文件\n").arg(files.size() - 15);
    }
    msg += QString(30, QChar(0x2500)) + QStringLiteral("\n");
    msg += QStringLiteral("点击 \"分析 #\" 选择序号，或点击 \"分析最新\" 自动分析最新的日志");

    _downloadedLogsCache = arr;
    emit downloadedLogsList(arr);
    emit messageReceived(QStringLiteral("system"), msg);
}

void AgentManager::analyzeLogByIndex(int index)
{
    if (index < 0 || index >= _downloadedLogsCache.size()) {
        emit messageReceived(QStringLiteral("system"), QStringLiteral("无效的日志序号: ") + QString::number(index + 1));
        return;
    }
    QJsonObject entry = _downloadedLogsCache[index].toObject();
    QString path = entry[QStringLiteral("path")].toString();
    runLogAnalysis(path, QStringLiteral("all"));
}

void AgentManager::downloadOnboardLogs()
{
    if (!MultiVehicleManager::instance()->activeVehicle()) {
        emit messageReceived(QStringLiteral("system"), QStringLiteral("无飞行器连接，无法下载机载日志"));
        return;
    }

    if (!_onboardLogCtrl) {
        _onboardLogCtrl = new OnboardLogController(this);

        connect(_onboardLogCtrl, &OnboardLogController::requestingListChanged, this,
                &AgentManager::_onOnboardLogListComplete);

        connect(_onboardLogCtrl, &OnboardLogController::downloadingLogsChanged, this,
                &AgentManager::_onOnboardLogDownloadComplete);
    }

    _statusText = QStringLiteral("请求机载日志列表...");
    emit statusTextChanged();
    emit messageReceived(QStringLiteral("system"), QStringLiteral("📡 正在请求机载日志列表..."));

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
        emit messageReceived(QStringLiteral("system"), QStringLiteral("请先请求机载日志列表"));
        return;
    }
    auto *model = qobject_cast<QmlObjectListModel *>(
        _onboardLogCtrl->property("model").value<QObject *>());
    if (!model || index < 0 || index >= model->count()) {
        emit messageReceived(QStringLiteral("system"), QStringLiteral("日志索引无效"));
        return;
    }
    auto *entry = model->value<QGCOnboardLogEntry *>(index);
    if (!entry) {
        emit messageReceived(QStringLiteral("system"), QStringLiteral("日志条目无效"));
        return;
    }
    entry->setSelected(true);
    QString logInfo = QStringLiteral("📥 正在下载 #%1 (%2)")
        .arg(entry->id())
        .arg(entry->size() < 1024 * 1024
            ? QString::number(entry->size() / 1024.0, 'f', 1) + QStringLiteral("KB")
            : QString::number(entry->size() / (1024.0 * 1024.0), 'f', 1) + QStringLiteral("MB"));
    emit messageReceived(QStringLiteral("system"), logInfo);
    emit onboardDownloadProgress(logInfo);
    _statusText = QStringLiteral("下载机载日志中...");
    emit statusTextChanged();
    _onboardLogCtrl->download(_onboardDownloadDir);
}

// ---- ReAct signal forwarding ----

void AgentManager::_onReActStarted()
{
    _isProcessing = true;
    emit isProcessingChanged();
    _streamAccum.clear();
    emit streamingTextChanged();
    _thinkingAccum.clear();
    if (_thinkThrottle) _thinkThrottle->stop();
    emit thinkingTextChanged();
    _lastThought.clear();
    _lastToolAction.clear();
}

void AgentManager::_onReActStopped()
{
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
    _statusText = _isAutonomous ? QStringLiteral("自主模式待命中") : QStringLiteral("就绪");
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
    emit streamingTextChanged();
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

    _statusText = QStringLiteral("等待确认: %1").arg(cmdDesc);
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
            : QStringLiteral("✓ %1 完成").arg(toolName);
        emit commandExecuted(result);
    }
}

void AgentManager::_onReActThought(const QString &thought)
{
    _lastThought = thought;
    if (_isAutonomous) {
        emit autonomousStep(thought, QString());
    } else {
        _statusText = QStringLiteral("思考: %1").arg(thought.left(80));
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
        _statusText = QStringLiteral("执行: %1").arg(actionDesc);
        emit statusTextChanged();
    }
}

void AgentManager::_onReActFinalAnswer(const QString &answer)
{
    if (_isAutonomous) {
        emit autonomousStep(answer, QStringLiteral("[任务完成]"));
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
    _statusText = QStringLiteral("就绪");
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
    _statusText = QStringLiteral("错误");
    emit statusTextChanged();
    emit messageReceived(QStringLiteral("system"), QStringLiteral("✗ 视觉分析失败: ") + error);
}

// ---- Onboard log slots ----

void AgentManager::_onOnboardLogListComplete()
{
    if (_onboardLogCtrl->property("requestingList").toBool()) return;

    auto *model = qobject_cast<QmlObjectListModel *>(
        _onboardLogCtrl->property("model").value<QObject *>());
    if (!model || model->count() == 0) {
        emit messageReceived(QStringLiteral("system"), QStringLiteral("未找到机载日志"));
        _statusText = QStringLiteral("就绪");
        emit statusTextChanged();
        return;
    }

    // Build list message for UI
    QStringList lines;
    lines.append(QStringLiteral("📡 机载日志列表 (%1 个):").arg(model->count()));
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
    lines.append(QStringLiteral("请在弹出的列表中选择要下载的日志"));

    emit messageReceived(QStringLiteral("system"), lines.join("\n"));
    emit onboardDownloadProgress(QStringLiteral("列表已加载"));
    _statusText = QStringLiteral("就绪");
    emit statusTextChanged();
}

void AgentManager::_onOnboardLogDownloadComplete()
{
    if (_onboardLogCtrl->property("downloadingLogs").toBool()) return;

    QDir dir(_onboardDownloadDir);
    QStringList filters = {QStringLiteral("*.ulg"), QStringLiteral("*.bin"), QStringLiteral("*.px4log")};
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Time);

    if (files.isEmpty()) {
        emit messageReceived(QStringLiteral("system"), QStringLiteral("下载完成但未找到日志文件"));
        _statusText = QStringLiteral("就绪");
        emit statusTextChanged();
        return;
    }

    QString path = files.first().absoluteFilePath();
    QString name = files.first().fileName();
    emit messageReceived(QStringLiteral("system"),
        QStringLiteral("✅ 下载完成: ") + name + QStringLiteral("\n开始自动分析..."));
    emit onboardDownloadComplete(path);
    _statusText = QStringLiteral("就绪");
    emit statusTextChanged();

    runLogAnalysis(path, QStringLiteral("all"));
}

QString AgentManager::_buildCommandDescription(const QString &toolName, const QJsonObject &p) const
{
    if (toolName == QStringLiteral("flight_control")) {
        QString action = p[QStringLiteral("action")].toString();
        if (action == QStringLiteral("takeoff")) {
            double alt = p[QStringLiteral("altitude")].toDouble(10);
            return QStringLiteral("起飞到 %1m").arg(alt);
        }
        if (action == QStringLiteral("land"))  return QStringLiteral("降落");
        if (action == QStringLiteral("rtl"))   return QStringLiteral("返航");
        if (action == QStringLiteral("pause")) return QStringLiteral("悬停");
        if (action == QStringLiteral("goto")) {
            double lat = p[QStringLiteral("latitude")].toDouble();
            double lon = p[QStringLiteral("longitude")].toDouble();
            double alt = p[QStringLiteral("altitude")].toDouble(50);
            return QStringLiteral("飞向 (%1, %2) 高度 %3m").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(alt);
        }
        if (action == QStringLiteral("orbit")) {
            double lat = p[QStringLiteral("latitude")].toDouble();
            double lon = p[QStringLiteral("longitude")].toDouble();
            double r = p[QStringLiteral("radius")].toDouble(30);
            return QStringLiteral("环绕 (%1, %2) 半径 %3m").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(r);
        }
        return QStringLiteral("飞控: %1").arg(action);
    }
    if (toolName == QStringLiteral("relative_move")) {
        QString dir = p[QStringLiteral("direction")].toString();
        double m = p[QStringLiteral("meters")].toDouble(5);
        if (dir == QStringLiteral("fwd"))  return QStringLiteral("前进 %1m").arg(m);
        if (dir == QStringLiteral("back")) return QStringLiteral("后退 %1m").arg(m);
        if (dir == QStringLiteral("left")) return QStringLiteral("左移 %1m").arg(m);
        if (dir == QStringLiteral("right")) return QStringLiteral("右移 %1m").arg(m);
        if (dir == QStringLiteral("turn")) return QStringLiteral("转向 %1°").arg(m);
        return QStringLiteral("相对移动: %1 %2m").arg(dir).arg(m);
    }
    if (toolName == QStringLiteral("altitude_speed")) {
        QString param = p[QStringLiteral("parameter")].toString();
        double val = p[QStringLiteral("value")].toDouble();
        if (param == QStringLiteral("altitude")) return QStringLiteral("高度变化 %1m").arg(val);
        if (param == QStringLiteral("speed"))    return QStringLiteral("地速设为 %1m/s").arg(val);
        return QStringLiteral("alt/speed: %1 = %2").arg(param).arg(val);
    }
    if (toolName == QStringLiteral("flight_mode")) {
        QString mode = p[QStringLiteral("mode")].toString();
        return QStringLiteral("切换模式: %1").arg(mode);
    }
    if (toolName == QStringLiteral("get_parameter")) {
        return QStringLiteral("查询参数: %1").arg(p[QStringLiteral("name")].toString());
    }
    if (toolName == QStringLiteral("set_parameter")) {
        return QStringLiteral("设置 %1 = %2").arg(p[QStringLiteral("name")].toString()).arg(p[QStringLiteral("value")].toDouble());
    }
    if (toolName == QStringLiteral("list_parameters")) {
        return QStringLiteral("列举参数: %1").arg(p[QStringLiteral("keyword")].toString());
    }
    if (toolName == QStringLiteral("mavlink_command")) {
        QString cmd = p[QStringLiteral("command_name")].toString();
        return QStringLiteral("MAVLink: %1").arg(cmd);
    }
    if (toolName == QStringLiteral("log_analysis")) {
        return QStringLiteral("日志分析: %1").arg(p[QStringLiteral("analysis_type")].toString(QStringLiteral("all")));
    }
    if (toolName == QStringLiteral("mission")) {
        QString action = p[QStringLiteral("action")].toString();
        if (action == QStringLiteral("add_takeoff")) {
            double alt = p[QStringLiteral("altitude")].toDouble(10);
            return QStringLiteral("设置起飞高度 %1m").arg(alt);
        }
        if (action == QStringLiteral("add_waypoint")) {
            double lat = p[QStringLiteral("latitude")].toDouble();
            double lon = p[QStringLiteral("longitude")].toDouble();
            double alt = p[QStringLiteral("altitude")].toDouble(50);
            return QStringLiteral("添加航点 (%1, %2, %3m)").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(alt);
        }
        if (action == QStringLiteral("remove_waypoint")) {
            return QStringLiteral("删除航点 #%1").arg(p[QStringLiteral("index")].toInt());
        }
        if (action == QStringLiteral("clear"))  return QStringLiteral("清空航点");
        if (action == QStringLiteral("list"))   return QStringLiteral("列出航点");
        if (action == QStringLiteral("upload"))  return QStringLiteral("上传任务");
        if (action == QStringLiteral("download")) return QStringLiteral("下载任务");
        return QStringLiteral("任务: %1").arg(action);
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
