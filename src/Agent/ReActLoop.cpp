#include "ReActLoop.h"
#include "LLMClient.h"
#include "ToolRegistry.h"
#include "AgentTool.h"
#include "ContextProvider.h"
#include "ConversationHistory.h"
#include "Vehicle.h"
#include "VehicleGPSFactGroup.h"
#include "BatteryFactGroupListModel.h"
#include "MultiVehicleManager.h"

#include <QtCore/QJsonDocument>

ReActLoop::ReActLoop(LLMClient *llm, ToolRegistry *tools, ContextProvider *ctx, QObject *parent)
    : QObject(parent)
    , _llm(llm)
    , _tools(tools)
    , _ctx(ctx)
{
    connect(_llm, &LLMClient::responseReceived, this, &ReActLoop::_onLLMResponse);
    connect(_llm, &LLMClient::responseReceivedWithTools, this, &ReActLoop::_onLLMResponseWithTools);
    connect(_llm, &LLMClient::streamChunk, this, &ReActLoop::_onStreamChunk);
    connect(_llm, &LLMClient::thinkingChunk, this, &ReActLoop::_onThinkingChunk);
    connect(_llm, &LLMClient::requestError, this, &ReActLoop::_onLLMError);
}

void ReActLoop::start(const QString &userMessage, bool autonomous)
{
    _iteration = 0;
    _isRunning = true;
    _autonomous = autonomous;
    _awaitingConfirmation = false;
    _thinkingAccum.clear();

    // Load previous conversation history (without system prompt)
    if (_history) {
        QJsonArray history = _history->load();
        // Trim to leave room for the fresh system prompt + new user message
        // and the ReAct iterations that will follow.
        if (history.size() > 30) {
            QJsonArray trimmed;
            int start = history.size() - 30;
            // Don't orphan tool messages: ensure the first message isn't a
            // dangling tool response whose preceding tool_calls was trimmed off.
            while (start > 0 && history[start].toObject()[QStringLiteral("role")].toString() == QStringLiteral("tool")) {
                start--;
            }
            for (int i = start; i < history.size(); i++) {
                trimmed.append(history[i]);
            }
            _messages = trimmed;
        } else {
            _messages = history;
        }
    } else {
        _messages = QJsonArray{};
    }

    // Build system prompt with tool descriptions (always fresh)
    _appendMessage(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), _buildSystemPrompt()}
    });

    // Build user message with vehicle context
    QString context = _ctx ? _ctx->buildVehicleContext() : QString();
    QString enrichedUserMsg = userMessage;
    if (!context.isEmpty()) {
        enrichedUserMsg += QStringLiteral("\n\n[当前飞行器状态]\n") + context;
    }

    _appendMessage(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), enrichedUserMsg}
    });

    emit started();
    _step();
}

void ReActLoop::cancel()
{
    _isRunning = false;
    _awaitingConfirmation = false;
    _llm->cancelRequest();
    emit stopped();
}

void ReActLoop::confirmToolExecution()
{
    if (!_awaitingConfirmation || !_isRunning) return;
    _awaitingConfirmation = false;

    auto *tool = _tools->getTool(_pendingToolName);
    if (!tool) {
        QJsonObject err;
        err[QStringLiteral("error")] = QStringLiteral("Unknown tool");
        _appendToolResult(_pendingToolName, err);
        _step();
        return;
    }

    _executeTool(_pendingToolName, _pendingToolArgs);
}

void ReActLoop::rejectToolExecution()
{
    if (!_awaitingConfirmation) return;
    _awaitingConfirmation = false;

    // Append rejection as tool result
    QJsonObject rejection;
    rejection[QStringLiteral("error")] = QStringLiteral("User rejected the tool execution");
    _appendToolResult(_pendingToolName, rejection);

    if (_isRunning) {
        _step();
    }
}

void ReActLoop::_step()
{
    if (!_isRunning) return;
    if (_maxIterations > 0 && _iteration >= _maxIterations) {
        _handleFinalResponse(QStringLiteral("[达到最大迭代次数 %1]").arg(_maxIterations));
        return;
    }

    _iteration++;
    qWarning() << "\n>>>>>> ReAct 迭代 #" << _iteration << " <<<<<<";
    emit iterationCompleted(_iteration, _maxIterations);
    _sendToLLM();
}

void ReActLoop::_sendToLLM()
{
    if (_apiEndpoint.isEmpty() || _apiKey.isEmpty()) {
        _onLLMError(QStringLiteral("API endpoint or key not set"));
        return;
    }

    QJsonArray tools = _tools->generateOpenAITools();
    _llm->sendFunctionRequest(_apiEndpoint, _apiKey, _modelName,
                               QString() /* system prompt already in messages */,
                               _messages, tools, true /* stream */);
}

void ReActLoop::_onLLMResponse(const QString &content)
{
    if (!_isRunning) return;
    qWarning() << "\n==========================================";
    qWarning() << "ReAct [" << _iteration << "] LLM 回复 (纯文本，无工具调用)";
    qWarning() << "==========================================";
    qWarning().noquote() << content;
    qWarning() << "==========================================\n";
    _handleFinalResponse(content);
}

void ReActLoop::_onLLMResponseWithTools(const QString &content, const QJsonObject &functionCall)
{
    if (!_isRunning) return;

    QString reasoning = functionCall[QStringLiteral("_reasoning")].toString();

    qWarning() << "\n==========================================";
    qWarning() << "ReAct [" << _iteration << "] LLM 回复 (含工具调用)";
    qWarning() << "==========================================";
    if (!reasoning.isEmpty()) {
        qWarning() << "--- 思考过程 (reasoning) ---";
        qWarning().noquote() << reasoning;
    }
    if (!content.isEmpty()) {
        qWarning() << "--- 回复内容 (content) ---";
        qWarning().noquote() << content;
    }
    if (!functionCall.isEmpty()) {
        QString fcName = functionCall[QStringLiteral("name")].toString();
        QString fcArgs = functionCall[QStringLiteral("arguments")].toString();
        qWarning() << "--- 工具调用 (function_call) ---";
        qWarning() << "  name:" << fcName;
        qWarning() << "  arguments:" << fcArgs;
    }
    qWarning() << "==========================================\n";

    // Emit thinking/reasoning content
    if (!content.isEmpty()) {
        emit thoughtProduced(content);
    }

    if (!functionCall.isEmpty()) {
        ToolRegistry::ParsedFunctionCall parsed = ToolRegistry::parseFunctionCall(functionCall);
        if (parsed.name.isEmpty() || parsed.arguments.isEmpty()) {
            _onLLMError(QStringLiteral("Failed to parse function call"));
            return;
        }

        int numCalls = parsed.arguments.size();
        qWarning() << "  →" << numCalls << "argument set(s), executing sequentially";
        qWarning() << "  [DEBUG] calling getTool...";

        auto *tool = _tools->getTool(parsed.name);
        qWarning() << "  [DEBUG] getTool returned:" << (tool ? tool->name() : "NULL");
        if (!tool) {
            QJsonObject err;
            err[QStringLiteral("error")] = QStringLiteral("Unknown tool: %1").arg(parsed.name);
            _appendToolResult(parsed.name, err);
            _step();
            return;
        }

        // Build assistant message with tool_calls (before confirmation check,
        // so tool_call_id is valid when user confirms/rejects later)
        QJsonObject firstArgs = parsed.arguments.first().toObject();
        _toolCallId = QStringLiteral("call_%1_%2").arg(parsed.name).arg(_iteration);

        QJsonObject tcFunc;
        tcFunc[QStringLiteral("name")] = parsed.name;
        tcFunc[QStringLiteral("arguments")] = QString::fromUtf8(QJsonDocument(parsed.arguments).toJson(QJsonDocument::Compact));

        QJsonObject tc;
        tc[QStringLiteral("id")] = _toolCallId;
        tc[QStringLiteral("type")] = QStringLiteral("function");
        tc[QStringLiteral("function")] = tcFunc;

        QJsonArray toolCalls;
        toolCalls.append(tc);

        QJsonObject assistantMsg;
        assistantMsg[QStringLiteral("role")] = QStringLiteral("assistant");
        if (!reasoning.isEmpty()) {
            assistantMsg[QStringLiteral("reasoning_content")] = reasoning;
        }
        if (!content.isEmpty()) {
            assistantMsg[QStringLiteral("content")] = content;
        }
        assistantMsg[QStringLiteral("tool_calls")] = toolCalls;
        _appendMessage(assistantMsg);

        emit functionCallReceived(parsed.name, firstArgs);

        // Check confirmation BEFORE executing
        if (!_autonomous && tool->requiresConfirmation()) {
            _pendingToolName = parsed.name;
            _pendingToolArgs = firstArgs;
            _awaitingConfirmation = true;
            emit toolConfirmationRequired(parsed.name, tool->description(), firstArgs);
            return;
        }

        // Execute tool for EACH argument set, collect combined results
        QJsonObject combined;
        QJsonArray results;
        for (int i = 0; i < numCalls; i++) {
            QJsonObject args = parsed.arguments[i].toObject();
            qWarning() << "  [DEBUG] executing tool call" << i << "/" << numCalls;
            emit toolInvoked(parsed.name, args);

            // Safety check (battery, GPS, preExecutionCheck) — previously
            // only ran in _executeTool (confirmation path). Now covers
            // the autonomous / no-confirmation path as well.
            QJsonObject safetyErr = _checkToolSafety(parsed.name, args, tool);
            if (safetyErr.contains(QStringLiteral("error"))) {
                emit toolResult(parsed.name, safetyErr);
                results.append(safetyErr);
            } else {
                QJsonObject observation = tool->execute(args);
                qWarning() << "  [DEBUG] tool call" << i << "done";
                emit toolResult(parsed.name, observation);
                results.append(observation);

                qWarning() << "--- 工具执行结果[" << i << "] (" << parsed.name << ") ---";
                qWarning().noquote() << QString::fromUtf8(QJsonDocument(observation).toJson(QJsonDocument::Compact));
                qWarning() << "---------------------------------------------";
            }
        }

        if (numCalls == 1) {
            combined = results.first().toObject();
        } else {
            combined[QStringLiteral("status")] = QStringLiteral("success");
            combined[QStringLiteral("num_calls")] = numCalls;
            combined[QStringLiteral("results")] = results;
            combined[QStringLiteral("result")] = QStringLiteral("Executed %1 calls. See results array for details.").arg(numCalls);
        }

        _appendToolResult(parsed.name, combined);

        if (_isRunning) {
            _step();
        }
    } else if (!content.isEmpty()) {
        // Text-only response (no function call) - this is the final answer
        QJsonObject msg;
        msg[QStringLiteral("role")] = QStringLiteral("assistant");
        msg[QStringLiteral("content")] = content;
        _appendMessage(msg);
        _handleFinalResponse(content);
    }
}

void ReActLoop::_executeTool(const QString &toolName, const QJsonObject &arguments)
{
    auto *tool = _tools->getTool(toolName);
    if (!tool) return;

    // Safety check (battery, GPS, preExecutionCheck)
    QJsonObject safetyErr = _checkToolSafety(toolName, arguments, tool);
    if (safetyErr.contains(QStringLiteral("error"))) {
        _appendToolResult(toolName, safetyErr);
        emit toolResult(toolName, safetyErr);
        _step();
        return;
    }

    // Execute the tool
    QJsonObject observation = tool->execute(arguments);

    qWarning() << "--- 工具执行结果 (" << toolName << ") ---";
    qWarning().noquote() << QString::fromUtf8(QJsonDocument(observation).toJson(QJsonDocument::Compact));
    qWarning() << "-------------------------------------------";

    // Emit result
    emit toolResult(toolName, observation);

    // Append observation as tool role message (modern OpenAI format)
    _appendToolResult(toolName, observation);

    if (_isRunning) {
        _step();
    }
}

QJsonObject ReActLoop::_checkToolSafety(const QString &toolName, const QJsonObject &arguments, AgentTool *tool)
{
    if (tool->isReadOnly()) {
        return {};
    }

    Vehicle *v = MultiVehicleManager::instance()->activeVehicle();

    // Determine if this is an emergency action (always allowed even with low battery)
    bool isEmergencyAction = false;
    if (toolName == QStringLiteral("flight_control")) {
        QString action = arguments[QStringLiteral("action")].toString();
        isEmergencyAction = (action == QStringLiteral("land")
                          || action == QStringLiteral("rtl")
                          || action == QStringLiteral("pause"));
    }

    // Battery check — applies to ALL non-readonly, non-emergency tools
    if (!isEmergencyAction && v) {
        auto *batteries = v->batteries();
        if (batteries && batteries->count() > 0) {
            auto *batGroup = dynamic_cast<BatteryFactGroup *>(batteries->get(0));
            if (batGroup && batGroup->percentRemaining()) {
                float pct = static_cast<float>(batGroup->percentRemaining()->rawValue().toDouble());
                if (pct < 15.0f) {
                    QJsonObject err;
                    err[QStringLiteral("error")] = QStringLiteral("Battery too low (%.1f%%), write operations blocked").arg(pct);
                    return err;
                }
            }
        }
    }

    // GPS check — only for movement operations (flight_control takeoff/goto/orbit)
    if (toolName == QStringLiteral("flight_control")) {
        QString action = arguments[QStringLiteral("action")].toString();
        static const QStringList gpsRequiredActions = {
            QStringLiteral("takeoff"), QStringLiteral("goto"), QStringLiteral("orbit")
        };
        if (gpsRequiredActions.contains(action)) {
            auto *gps = v ? dynamic_cast<VehicleGPSFactGroup *>(v->gpsFactGroup()) : nullptr;
            if (gps && gps->lock()) {
                int lock = static_cast<int>(gps->lock()->rawValue().toDouble());
                if (lock < 3) {
                    QJsonObject err;
                    err[QStringLiteral("error")] = QStringLiteral("GPS lock < 3, movement operations blocked");
                    return err;
                }
            }
        }
    }

    // Pre-execution check from the tool itself
    if (v) {
        QString preCheckErr = tool->preExecutionCheck(arguments, v);
        if (!preCheckErr.isEmpty()) {
            QJsonObject err;
            err[QStringLiteral("error")] = preCheckErr;
            return err;
        }
    }

    return {};
}

void ReActLoop::_handleFinalResponse(const QString &content)
{
    qWarning() << "\n********** ReAct 最终回答 **********";
    qWarning().noquote() << (content.isEmpty() ? QStringLiteral("[任务完成]") : content);
    qWarning() << "**************************************\n";

    if (content.isEmpty()) {
        emit finalAnswer(QStringLiteral("[任务完成]"));
    } else {
        emit finalAnswer(content);
    }
    emit messageProduced(QStringLiteral("assistant"), content.isEmpty() ? QStringLiteral("[任务完成]") : content);
    _isRunning = false;

    // Persist conversation history for the next turn
    if (_history) {
        _history->save(_messages);
    }

    emit stopped();
}

void ReActLoop::_onStreamChunk(const QString &text)
{
    emit streamingChunk(text);
}

void ReActLoop::_onThinkingChunk(const QString &text)
{
    _thinkingAccum += text;
    emit thinkingChunk(text);
}

void ReActLoop::_onLLMError(const QString &error)
{
    if (!_isRunning) return;
    _isRunning = false;
    _awaitingConfirmation = false;
    emit messageProduced(QStringLiteral("system"), QStringLiteral("Error: %1").arg(error));
    emit stopped();
}

void ReActLoop::_appendMessage(const QJsonObject &msg)
{
    _messages.append(msg);

    // Trim history to prevent token limit errors (keep system + last 38 messages)
    if (_messages.size() > 40) {
        QJsonObject systemMsg = _messages.first().toObject();
        QJsonArray trimmed;
        trimmed.append(systemMsg);
        int start = _messages.size() - 38;

        // Don't orphan tool messages: if the first message after system
        // is a "tool" role, walk back to include its preceding "assistant"
        // message with tool_calls.
        while (start > 1
               && _messages[start].toObject().value(QStringLiteral("role")).toString()
                      == QStringLiteral("tool")) {
            start--;
        }

        for (int i = start; i < _messages.size(); i++) {
            trimmed.append(_messages[i].toObject());
        }
        _messages = trimmed;
    }
}

void ReActLoop::_appendToolResult(const QString &toolName, const QJsonObject &observation)
{
    Q_UNUSED(toolName);
    QString contentStr = QString::fromUtf8(QJsonDocument(observation).toJson(QJsonDocument::Compact));
    QJsonObject toolMsg;
    toolMsg[QStringLiteral("role")] = QStringLiteral("tool");
    toolMsg[QStringLiteral("tool_call_id")] = _toolCallId;
    toolMsg[QStringLiteral("content")] = contentStr;
    _appendMessage(toolMsg);
}

QString ReActLoop::_buildSystemPrompt() const
{
    QString prompt;
    prompt += QStringLiteral(
        "You are AgentQGC AI, a professional drone flight assistant. You control the drone via function calling.\n"
        "VERY IMPORTANT: Always respond in Chinese. The user speaks Chinese.\n\n"
        "## Core Rules (MUST follow!)\n"
        "1. Start every task with get_vehicle_state to assess current conditions\n"
        "2. You can call the same tool multiple times in one turn by providing multiple JSON objects: "
        "{\"keyword\":\"BAT\"}{\"keyword\":\"GPS\"} — the system will execute each sequentially\n"
        "3. When done, give the COMPLETE final report immediately. Do NOT say 'let me organize' — give the actual report\n"
        "4. ⚠️ Generate VALID JSON arguments only! Double-check quotes, commas, and braces are paired correctly\n\n"
        "## Response Style\n"
        "- Be thorough and explanatory — don't just dump raw data, interpret it for the user\n"
        "- Structure your final response with clear sections and bullet points\n"
        "- For diagnostics: explain what each value means, whether it's normal/abnormal, and why\n"
        "- For actions: state what you're about to do, why, and what the user should expect\n"
        "- End every diagnostic report with a clear verdict and actionable recommendation\n"
        "- Use emoji sparingly for readability (✅ ⚠️ ❌)\n\n"
        "## Tool Reference\n"
        "- get_vehicle_state: no params required — always check firmware type first\n"
        "- get_parameter: {\"name\": \"PARAM_NAME\"}\n"
        "- set_parameter: {\"name\": \"PARAM_NAME\", \"value\": 123.4}\n"
        "- list_parameters: {\"keyword\": \"PID\"} — can batch: {\"keyword\":\"BAT\"}{\"keyword\":\"GPS\"}\n"
        "- flight_control: {\"action\": \"takeoff|land|rtl|pause|goto|orbit\"}. "
        "takeoff/goto/orbit/pause require vehicle to be in guided mode (PX4: Hold; ArduPilot: GUIDED). "
        "Switch mode first with flight_mode before these actions!\n"
        "- flight_mode: {\"mode\": \"<name>\"}. PX4 modes: Manual, Hold, Takeoff, Land, Return, Position, Altitude, Mission, Offboard. "
        "ArduPilot modes: GUIDED, LOITER, RTL, LAND, AUTO, STABILIZE. "
        "Always check firmware type and pick the correct mode!\n"
        "- relative_move: {\"direction\": \"fwd|back|left|right|turn\", \"meters\": 5}\n"
        "- altitude_speed: {\"parameter\": \"altitude|speed\", \"value\": 10}\n"
        "- mavlink_command: {\"command_name\": \"MAV_CMD_XXX\", \"params\": [p1,p2,...], \"use_int\": false}\n"
        "- analyze_flight_log: {\"analysis_type\": \"vibration|gps|battery|ekf|all\"}\n"
        "- get_location_address: mode=\"reverse\" (default) or mode=\"forward\" with address=\"地点名\"\n\n"
        "## PX4 Flight Workflow (when firmware is PX4 Pro)\n"
        "To take off and fly: flight_mode(mode=\"Hold\") → flight_control(action=\"takeoff\") → flight_control(action=\"goto\", param1=lat, param2=lon)\n\n"
        "## Safety Rules\n"
        "- land and rtl are always allowed, no confirmation needed\n"
        "- Battery < 15%: ALL write operations blocked\n"
        "- No 3D GPS lock: takeoff/goto/orbit blocked\n"
        "- Immediately warn user of any abnormality (low battery, GPS loss, high vibration)\n"
    );

    return prompt;
}

void ReActLoop::setConversationHistory(ConversationHistory *history)
{
    _history = history;
}

void ReActLoop::clearHistory()
{
    if (_history) {
        _history->clear();
    }
    _messages = QJsonArray{};
}

bool ReActLoop::hasHistory() const
{
    return _history && !_history->isEmpty();
}
