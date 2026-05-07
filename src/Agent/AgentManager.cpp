#include "AgentManager.h"
#include "LLMClient.h"
#include "ContextProvider.h"
#include "CommandEngine.h"
#include "Vehicle.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(AgentManagerLog, "qgc.agent")

AgentManager *AgentManager::instance()
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
    , _cmd(CommandEngine::instance())
{
    connect(_llm, &LLMClient::responseReceived, this, &AgentManager::_onResponseReceived);
    connect(_llm, &LLMClient::streamChunk, this, &AgentManager::_onStreamChunk);
    connect(_llm, &LLMClient::requestError, this, &AgentManager::_onRequestError);

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

void AgentManager::setApiKey(const QString &key) { _apiKey = key; }
void AgentManager::setApiEndpoint(const QString &url) { _apiEndpoint = url; }
void AgentManager::setModelName(const QString &model) { _modelName = model; }

void AgentManager::sendMessage(const QString &userMessage)
{
    if (_isProcessing || userMessage.trimmed().isEmpty()) return;
    if (_apiKey.isEmpty()) {
        _errorMessage = QStringLiteral("请设置 API Key");
        emit errorMessageChanged();
        return;
    }
    _errorMessage.clear();
    emit errorMessageChanged();
    _isProcessing = true;
    emit isProcessingChanged();
    _statusText = QStringLiteral("思考中...");
    emit statusTextChanged();

    _chatHistory.append(QStringLiteral("user:") + userMessage);
    emit messageReceived(QStringLiteral("user"), userMessage);

    const QString prompt = _buildSystemPrompt(userMessage);
    _llm->sendRequest(_apiEndpoint, _apiKey, _modelName, prompt, _chatHistory);
}

void AgentManager::cancelRequest()
{
    _llm->cancelRequest();
    _isProcessing = false;
    emit isProcessingChanged();
    _statusText = QStringLiteral("已取消");
    emit statusTextChanged();
}

void AgentManager::confirmCommand(const QVariantMap &cmdMap)
{
    Q_UNUSED(cmdMap);
    if (_pendingCommand.isValid) {
        _cmd->executeCommand(_pendingCommand);
        _pendingCommand = {};
    }
}

void AgentManager::rejectCommand()
{
    _pendingCommand = {};
    emit messageReceived(QStringLiteral("system"), QStringLiteral("✗ 指令已取消"));
}

void AgentManager::runQuickCheck(const QString &checkType)
{
    static const QHash<QString, QString> prompts = {
        {"battery",    QStringLiteral("检查电池状态，包括电压、电量、温度、电流。有异常明确警告。")},
        {"gps",        QStringLiteral("检查GPS状态，卫星数、HDOP、定位类型，判断是否满足安全飞行。")},
        {"vibration",  QStringLiteral("分析当前震动水平，震动过高给出排查建议。")},
        {"preflight",  QStringLiteral("执行完整飞前检查：电池、GPS、传感器、遥控器、螺旋桨、安全开关。")},
        {"params",     QStringLiteral("检查飞控参数异常：PID、校准、安全开关配置。")}
    };
    sendMessage(prompts.value(checkType, checkType));
}

void AgentManager::_onResponseReceived(const QString &content)
{
    _isProcessing = false;
    emit isProcessingChanged();
    _statusText = QStringLiteral("就绪");
    emit statusTextChanged();

    if (!content.isEmpty()) {
        _chatHistory.append(QStringLiteral("assistant:") + content);
        emit messageReceived(QStringLiteral("assistant"), content);
        if (_chatHistory.size() > kMaxHistory) {
            _chatHistory = _chatHistory.last(kMaxHistory - 2);
        }

        // Check for embedded commands
        AgentCommand cmd = _cmd->parseCommand(content);
        if (cmd.isValid && MultiVehicleManager::instance()->activeVehicle()) {
            if (cmd.isReadOnly) {
                _cmd->executeCommand(cmd);
            } else {
                _pendingCommand = cmd;
                QVariantMap m;
                m[QStringLiteral("action")] = cmd.action;
                QVariantList plist;
                for (double d : cmd.params) plist.append(d);
                m[QStringLiteral("params")] = plist;
                m[QStringLiteral("raw")] = cmd.raw;
                emit commandPending(_cmd->describeCommand(cmd), m);
            }
        }
    }
}

void AgentManager::_onStreamChunk(const QString &text)
{
    emit streamingChunk(text);
}

void AgentManager::_onRequestError(const QString &error)
{
    _isProcessing = false;
    emit isProcessingChanged();
    _errorMessage = error;
    emit errorMessageChanged();
    _statusText = QStringLiteral("错误");
    emit statusTextChanged();
}

QString AgentManager::_buildSystemPrompt(const QString &userMessage)
{
    Q_UNUSED(userMessage);
    QString sp;
    sp += QStringLiteral("你是专业无人机调参助手，精通PX4/ArduPilot。简要回答，发现隐患必须警告。中文。\n\n");

    sp += QStringLiteral("飞控指令 (用<cmd>标签，执行前用户确认):\n");
    sp += QStringLiteral("  <cmd>takeoff [高度m]</cmd> 起飞\n");
    sp += QStringLiteral("  <cmd>land</cmd> 降落\n");
    sp += QStringLiteral("  <cmd>rtl</cmd> 返航\n");
    sp += QStringLiteral("  <cmd>pause</cmd> 悬停\n");
    sp += QStringLiteral("  <cmd>alt [+/-]数值</cmd> 改变高度\n");
    sp += QStringLiteral("  <cmd>speed 数值</cmd> 改变地速\n");
    sp += QStringLiteral("  <cmd>goto lat lon [alt]</cmd> 飞向坐标\n");
    sp += QStringLiteral("  <cmd>orbit lat lon alt [半径]</cmd> 环绕\n");
    sp += QStringLiteral("参数控制:\n");
    sp += QStringLiteral("  <cmd>paramq 参数名</cmd> 查询参数\n");
    sp += QStringLiteral("  <cmd>paramlist 关键字</cmd> 列出匹配参数\n");
    sp += QStringLiteral("  <cmd>param 参数名 值</cmd> 修改参数(需确认!)\n\n");

    sp += _ctx->buildVehicleContext();
    return sp;
}
