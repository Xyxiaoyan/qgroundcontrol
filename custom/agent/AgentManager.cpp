#include "AgentManager.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtQml/qqmlengine.h>

#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "Fact.h"
#include "ParameterManager.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(AgentLog, "qgc.agent")

AgentManager::AgentManager(QObject *parent)
    : QObject(parent)
    , _network(new QNetworkAccessManager(this))
{
    _network->setTransferTimeout(kRequestTimeoutMs);
}

AgentManager::~AgentManager()
{
    cancelRequest();
}

AgentManager *AgentManager::instance()
{
    static AgentManager *_instance = nullptr;
    if (!_instance) {
        _instance = new AgentManager();
    }
    return _instance;
}

void AgentManager::setApiEndpoint(const QString &endpoint)
{
    if (_apiEndpoint != endpoint) {
        _apiEndpoint = endpoint;
        emit apiEndpointChanged();
    }
}

void AgentManager::setApiKey(const QString &key)
{
    if (_apiKey != key) {
        _apiKey = key;
        emit apiKeyChanged();
    }
}

void AgentManager::setModelName(const QString &model)
{
    if (_modelName != model) {
        _modelName = model;
        emit modelNameChanged();
    }
}

void AgentManager::sendMessage(const QString &userMessage)
{
    if (_isProcessing || userMessage.trimmed().isEmpty()) {
        return;
    }

    if (_apiKey.isEmpty()) {
        _setError("API Key not configured. Please set it in Settings > Agent.");
        return;
    }

    _setError("");
    _setProcessing(true);

    _history.append({"user", userMessage});
    emit messageReceived("user", userMessage);

    const QJsonArray messages = _buildMessagesArray(userMessage);
    QJsonObject body;
    body.insert("model", _modelName);
    body.insert("messages", messages);
    body.insert("stream", true);
    body.insert("temperature", 0.3);

    QNetworkRequest request(_apiEndpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(_apiKey).toUtf8());

    _currentReply = _network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(_currentReply, &QNetworkReply::finished, this, &AgentManager::_onReplyFinished);
    connect(_currentReply, &QNetworkReply::readyRead, this, &AgentManager::_onReadyRead);
}

void AgentManager::cancelRequest()
{
    if (_currentReply && _currentReply->isRunning()) {
        _currentReply->abort();
    }
    _setProcessing(false);
}

void AgentManager::clearHistory()
{
    _history.clear();
    emit historyCleared();
}

void AgentManager::runQuickCheck(const QString &checkType)
{
    QString prompt;
    if (checkType == "battery") {
        prompt = "请检查当前飞行器电池状态，包括电压、电量百分比、温度、放电电流，并判断是否存在异常。如果有异常请给出具体警告。";
    } else if (checkType == "gps") {
        prompt = "请检查当前GPS状态，包括卫星数量、HDOP、定位类型，并判断是否满足安全飞行条件。";
    } else if (checkType == "params") {
        prompt = "请检查飞控参数是否存在异常配置，重点关注PID参数、传感器校准状态、安全开关设置。";
    } else if (checkType == "vibration") {
        prompt = "请分析当前飞行器震动水平，如果震动过高请给出可能的原因和排查建议。";
    } else if (checkType == "preflight") {
        prompt = "请执行完整的飞行前检查清单，包括电池、GPS、传感器、遥控器信号、螺旋桨、电机、安全开关等所有项目。";
    } else {
        prompt = checkType;
    }
    sendMessage(prompt);
}

QString AgentManager::buildSystemContext()
{
    return _collectVehicleContext();
}

void AgentManager::_onReplyFinished(QNetworkReply *reply)
{
    if (reply != _currentReply) {
        reply->deleteLater();
        return;
    }

    _currentReply = nullptr;
    _setProcessing(false);

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            _setError(QString("Network error: %1").arg(reply->errorString()));
        }
        reply->deleteLater();
        return;
    }

    const QByteArray data = reply->readAll();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (statusCode != 200) {
        _setError(QString("API error %1: %2").arg(statusCode).arg(QString(data).left(200)));
        reply->deleteLater();
        return;
    }

    if (_currentStreamContent.isEmpty()) {
        _parseFinalResponse(data);
    } else {
        const QString assistantMessage = _currentStreamContent.trimmed();
        if (!assistantMessage.isEmpty()) {
            _history.append({"assistant", assistantMessage});
            emit messageReceived("assistant", assistantMessage);
        }
        _currentStreamContent.clear();
    }

    if (_history.size() > kMaxHistoryMessages) {
        _history = _history.last(kMaxHistoryMessages - 2);
    }

    reply->deleteLater();
}

void AgentManager::_onReadyRead()
{
    if (!_currentReply) return;

    const QByteArray data = _currentReply->readAll();
    _parseStreamingResponse(data);
}

QString AgentManager::_collectVehicleContext()
{
    Vehicle *vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!vehicle) {
        return "No active vehicle connected.";
    }

    QString ctx;
    ctx += "## 飞行器状态\n";
    ctx += QString("- 连接状态: %1\n").arg(vehicle->armed() ? "已解锁(Armed)" : "已锁定(Disarmed)");
    ctx += QString("- 飞行模式: %1\n").arg(vehicle->flightMode());
    ctx += QString("- 固件类型: %1\n").arg(vehicle->firmwareType() == Vehicle::FirmwareTypePX4 ? "PX4" :
                                             vehicle->firmwareType() == Vehicle::FirmwareTypeArduPilot ? "ArduPilot" : "Unknown");
    ctx += QString("- 飞行器类型: %1\n").arg(vehicle->vehicleType() == Vehicle::VehicleTypeMultiRotor ? "多旋翼" :
                                               vehicle->vehicleType() == Vehicle::VehicleTypeFixedWing ? "固定翼" : "Other");

    Fact *batPct = vehicle->batteryFact("batteryPercentRemaining");
    if (batPct) ctx += QString("- 电池电量: %1%\n").arg(batPct->cookedValue().toDouble(), 0, 'f', 1);

    Fact *batVolt = vehicle->batteryFact("voltage");
    if (batVolt) ctx += QString("- 电池电压: %1V\n").arg(batVolt->cookedValue().toDouble(), 0, 'f', 2);

    Fact *batCurrent = vehicle->batteryFact("current");
    if (batCurrent) ctx += QString("- 放电电流: %1A\n").arg(batCurrent->cookedValue().toDouble(), 0, 'f', 1);

    Fact *batTemp = vehicle->batteryFact("temperature");
    if (batTemp) ctx += QString("- 电池温度: %1°C\n").arg(batTemp->cookedValue().toDouble(), 0, 'f', 1);

    Fact *gpsCount = vehicle->gpsFact("count");
    if (gpsCount) ctx += QString("- GPS卫星数: %1\n").arg(gpsCount->rawValue().toInt());

    Fact *gpsHdop = vehicle->gpsFact("hdop");
    if (gpsHdop) ctx += QString("- GPS HDOP: %1\n").arg(gpsHdop->cookedValue().toDouble(), 0, 'f', 2);

    Fact *gpsLock = vehicle->gpsFact("lock");
    if (gpsLock) ctx += QString("- GPS定位: %1\n").arg(gpsLock->rawValue().toInt() >= 3 ? "3D Fix" : "No Fix");

    ctx += QString("- 飞行时间: %1s\n").arg(vehicle->flightTime()->rawValue().toInt());
    ctx += QString("- 错误码: %1\n").arg(vehicle->errorCode() != 0 ? QString("0x%1").arg(vehicle->errorCode(), 0, 16) : "None");

    return ctx;
}

QString AgentManager::_collectParameterSummary()
{
    Vehicle *vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!vehicle) return "";

    ParameterManager *paramMgr = vehicle->parameterManager();
    if (!paramMgr || !paramMgr->parametersReady()) return "Parameters not loaded.";

    QString summary;
    summary += "\n## 关键参数\n";

    QList<QString> keyParams = {"MC_ROLL_P", "MC_ROLL_I", "MC_PITCH_P", "MC_PITCH_I", "MC_YAW_P",
                                 "MPC_XY_VEL_P", "MPC_Z_VEL_P", "COM_DISARM_LAND", "COM_RC_OVERRIDE",
                                 "CBRK_SUPPLY_CHK", "BAT_N_CELLS", "BAT_V_CHARGED", "BAT_V_EMPTY"};

    for (const QString &paramName : keyParams) {
        Fact *fact = paramMgr->getParameter(-1, paramName);
        if (fact) {
            summary += QString("- %1: %2\n").arg(paramName).arg(fact->cookedValue().toDouble(), 0, 'f', 4);
        }
    }

    return summary;
}

QJsonArray AgentManager::_buildMessagesArray(const QString &userMessage)
{
    QJsonArray messages;

    QString systemPrompt = "你是一个专业的无人机调试助手，精通PX4和ArduPilot飞控系统。"
                           "你能分析飞行器状态、诊断问题、给出调参建议和安全检查清单。"
                           "回答要简洁、专业、重点突出。如果发现安全隐患，必须明确警告用户。"
                           "用中文回答。\n\n当前飞行器上下文：\n";
    systemPrompt += _collectVehicleContext();
    systemPrompt += _collectParameterSummary();

    messages.append(QJsonObject{
        {"role", "system"},
        {"content", systemPrompt}
    });

    int startIdx = qMax(0, _history.size() - 8);
    for (int i = startIdx; i < _history.size(); i++) {
        messages.append(QJsonObject{
            {"role", _history[i].role},
            {"content", _history[i].content}
        });
    }

    return messages;
}

void AgentManager::_setProcessing(bool processing)
{
    if (_isProcessing != processing) {
        _isProcessing = processing;
        emit isProcessingChanged();
    }
}

void AgentManager::_setError(const QString &error)
{
    if (_errorMessage != error) {
        _errorMessage = error;
        emit errorMessageChanged();
    }
}

void AgentManager::_parseStreamingResponse(const QByteArray &data)
{
    QString text(data);
    const QStringList lines = text.split('\n');

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (!trimmed.startsWith("data: ")) continue;

        trimmed = trimmed.mid(6);
        if (trimmed == "[DONE]") continue;

        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) continue;

        const QJsonObject obj = doc.object();
        const QJsonArray choices = obj["choices"].toArray();
        if (choices.isEmpty()) continue;

        const QJsonObject choice = choices[0].toObject();
        const QJsonObject delta = choice["delta"].toObject();
        const QString content = delta["content"].toString();

        if (!content.isEmpty()) {
            _currentStreamContent += content;
            emit streamingChunk(content);
        }
    }
}

void AgentManager::_parseFinalResponse(const QByteArray &data)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        _setError("Failed to parse API response");
        return;
    }

    const QJsonObject obj = doc.object();
    const QJsonArray choices = obj["choices"].toArray();
    if (choices.isEmpty()) {
        _setError("Empty response from API");
        return;
    }

    const QJsonObject choice = choices[0].toObject();
    const QJsonObject message = choice["message"].toObject();
    const QString content = message["content"].toString();

    if (!content.isEmpty()) {
        _history.append({"assistant", content});
        emit messageReceived("assistant", content);
    }
}
