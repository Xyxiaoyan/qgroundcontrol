import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.LocalStorage
import QtPositioning
import QGroundControl

Rectangle {
    id: agentPanel
    width: Math.min(320, parent.width * 0.8)
    height: parent.height
    color: "#1a1a2e"
    visible: false
    x: parent.width

    property bool isExpanded: false
    property bool showSettings: false
    property bool _isProcessing: false
    property string _errorMessage: ""
    property var _history: []
    property var _activeXhr: null
    property var _db: null

    property string _apiEndpoint: "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"
    property string _apiKey: ""
    property string _modelName: "qwen-plus"

    Component.onCompleted: { _openDb() }

    function _openDb() {
        try {
            _db = LocalStorage.openDatabaseSync("AgentSettings", "1.0", "AI Agent Config", 1000)
            if (_db) {
                _db.transaction(function(tx) {
                    tx.executeSql("CREATE TABLE IF NOT EXISTS config(key TEXT PRIMARY KEY, value TEXT)")
                })
                _loadConfig()
            }
        } catch(e) { _settingsStatus = "DB err:" + e }
    }

    function _loadConfig() {
        try {
            _db.readTransaction(function(tx) {
                var rs = tx.executeSql("SELECT key, value FROM config")
                for (var i = 0; i < rs.rows.length; i++) {
                    var row = rs.rows.item(i)
                    if (row.key === "apiEndpoint") _apiEndpoint = row.value
                    else if (row.key === "apiKey") _apiKey = row.value
                    else if (row.key === "model") _modelName = row.value
                }
                _settingsStatus = _apiKey ? "已加载" : "新配置"
            })
        } catch(e) { _settingsStatus = "load err:" + e }
    }

    function _saveConfig() {
        try {
            _db.transaction(function(tx) {
                tx.executeSql("INSERT OR REPLACE INTO config(key, value) VALUES('apiEndpoint', ?)", [_apiEndpoint])
                tx.executeSql("INSERT OR REPLACE INTO config(key, value) VALUES('apiKey', ?)", [_apiKey])
                tx.executeSql("INSERT OR REPLACE INTO config(key, value) VALUES('model', ?)", [_modelName])
            })
            _settingsStatus = "已保存"
        } catch(e) { _settingsStatus = "save err:" + e }
    }

    // ── Command infrastructure ──
    property var _pendingCmd: null
    property bool _showConfirm: false

    function _parseCommands(content) {
        var cmdRegex = /<cmd>([^<]+)<\/cmd>/gi
        var match
        while ((match = cmdRegex.exec(content)) !== null) {
            var cmd = match[1].trim()
            // Parse command: "ACTION param1 param2..."
            var parts = cmd.split(/\s+/)
            var action = parts[0].toLowerCase()
            var params = parts.slice(1).map(parseFloat)
            // Validate
            var valid = ["takeoff","land","rtl","pause","alt","speed","goto","orbit","param","paramq","paramlist"].indexOf(action) >= 0
            if (valid) return { action: action, params: params, raw: cmd, valid: valid }
        }
        return null
    }

    function _safe(p, def) { return (p !== undefined && !isNaN(p)) ? p : def }
    function _sf(p, digits, def) { var v = _safe(p, def); return v.toFixed(digits) }

    function _describeCmd(cmd) {
        var d = {
            "takeoff": "起飞 (" + _safe(cmd.params[0], 5) + "m)",
            "land": "降落",
            "rtl": "返航 (Return to Launch)",
            "pause": "悬停/暂停",
            "alt": "改变高度 " + (_safe(cmd.params[0], 0) > 0 ? "+" : "") + _safe(cmd.params[0], 0) + "m",
            "speed": "改变速度 " + _safe(cmd.params[0], 5) + "m/s",
            "goto": "飞向 (" + _sf(cmd.params[0], 6, 0) + ", " + _sf(cmd.params[1], 6, 0) + ")" + (_safe(cmd.params[2], -1) >= 0 ? " alt:" + _sf(cmd.params[2], 1, 0) + "m" : ""),
            "orbit": "环绕中心 alt:" + (_safe(cmd.params[2], 50) || 50) + "m, r:" + (_safe(cmd.params[3], 30) || 30) + "m",
            "param": "⚠ 修改参数: " + String(cmd.raw.split(/\s+/)[1] || "?") + " = " + _safe(cmd.params[1], 0),
            "paramq": "查询参数: " + String(cmd.raw.split(/\s+/)[1] || "?"),
            "paramlist": "列举参数: " + String(cmd.raw.split(/\s+/)[1] || "all")
        }
        return d[cmd.action] || cmd.raw
    }

    function _confirmCmd() {
        _showConfirm = false
        if (!_pendingCmd || !_veh) return
        var c = _pendingCmd
        try {
            switch (c.action) {
            case "takeoff":
                _veh.guidedModeTakeoff(_safe(c.params[0], 5))
                break
            case "land":
                _veh.guidedModeLand()
                break
            case "rtl":
                _veh.guidedModeRTL(false)
                break
            case "pause":
                _veh.pauseVehicle()
                break
            case "alt":
                _veh.guidedModeChangeAltitude(_safe(c.params[0], 0), true)
                break
            case "speed":
                _veh.guidedModeChangeGroundSpeedMetersSecond(_safe(c.params[0], 5))
                break
            case "goto": {
                var lat = _safe(c.params[0], 0)
                var lon = _safe(c.params[1], 0)
                var alt = _safe(c.params[2], 50)
                _veh.guidedModeGotoLocation(QtPositioning.coordinate(lon, lat, alt))
                break
            }
            case "orbit": {
                var la = _safe(c.params[0], 0)
                var lo = _safe(c.params[1], 0)
                var al = _safe(c.params[2], 50)
                var ra = _safe(c.params[3], 30)
                _veh.guidedModeOrbit(QtPositioning.coordinate(lo, la, al), ra, al)
                break
            }
            case "param":
                if (!_veh || !_veh.parameterManager) break
                _veh.parameterManager.getParameter(-1, String(c.raw.split(/\s+/)[1])).rawValue = c.params[1]
                chatModel.append({"role": "system", "content": "✓ 参数 " + c.raw.split(/\s+/)[1] + " = " + c.params[1]})
                break
            case "paramq": {
                if (!_veh || !_veh.parameterManager) break
                var nm = String(c.raw.split(/\s+/)[1] || "")
                var f = _veh.parameterManager.getParameter(-1, nm)
                var val = f ? (_rv(f, "?") + (f.units ? " " + f.units : "")) : "not found"
                chatModel.append({"role": "system", "content": "参数 " + nm + " = " + val})
                break
            }
            case "paramlist":
                if (!_veh || !_veh.parameterManager) break
                var pat = String(c.raw.split(/\s+/)[1] || "").toUpperCase()
                var matched = []
                var allNames = _veh.parameterManager.parameterNames(-1)
                for (var k = 0; k < allNames.length; k++) {
                    if (allNames[k].toUpperCase().indexOf(pat) >= 0) {
                        var pf = _veh.parameterManager.getParameter(-1, allNames[k])
                        matched.push(allNames[k] + "=" + _rv(pf, "?"))
                        if (matched.length >= 30) break
                    }
                }
                chatModel.append({"role": "system", "content": matched.length === 0 ? "无匹配参数" : matched.join("\n")})
                break
                var keys = groups[pat] || Object.keys(groups)
                for (var g in groups) {
                    if (g.indexOf(pat) >= 0 || pat === "") { keys = groups[g]; break }
                }
                if (keys === Object.keys(groups)) keys = groups["PID"]  // default to PID
                var out = []
                for (var j = 0; j < keys.length; j++) {
                    var pf = _veh.parameterManager.getParameter(-1, keys[j])
                    out.push(keys[j] + "=" + _rv(pf, "?"))
                }
                chatModel.append({"role": "system", "content": "参数组 " + pat + ":\n" + out.join("\n")})
                break
            }
            }
            chatModel.append({"role": "system", "content": "✓ 已执行: " + _describeCmd(c)})
        } catch(e) {
            _errorMessage = "指令执行失败: " + e
        }
        _pendingCmd = null
    }

    function _cancelCmd() {
        _showConfirm = false
        _pendingCmd = null
        chatModel.append({"role": "system", "content": "✗ 已取消"})
    }

    property string _settingsStatus: ""

    // Vehicle data via QML property bindings
    readonly property var _veh: QGroundControl.multiVehicleManager.activeVehicle
    readonly property string _modeVal: _veh ? _veh.flightMode : "--"
    readonly property var _batF: _veh && _veh.batteries && _veh.batteries.count > 0 ? _veh.batteries.get(0) : null
    readonly property var _gpsCntF: _veh && _veh.gps ? _veh.gps.count : null
    readonly property var _gpsLockF: _veh && _veh.gps ? _veh.gps.lock : null
    readonly property var _gpsHdopF: _veh && _veh.gps ? _veh.gps.hdop : null

    function _rv(fact, def) { try { return fact ? fact.rawValue : def } catch(e) { return def } }

    property bool _statusConnected: false
    property string _statusMode: "--"
    property string _statusBat: "?"
    property string _statusGps: "?"

    Timer {
        interval: 2000
        running: true
        repeat: true
        onTriggered: {
            _statusConnected = _veh !== null
            if (_statusConnected) {
                _statusMode = _modeVal
                var bat = _rv(_batF ? _batF.percentRemaining : null, -1)
                _statusBat = bat >= 0 ? bat.toFixed(0) : "?"
                var gps = _rv(_gpsCntF, 0)
                _statusGps = gps > 0 ? String(gps) : "?"
            }
        }
    }

    Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

    function open() {
        visible = true
        isExpanded = true
        x = parent.width - width
    }

    function close() {
        isExpanded = false
        x = parent.width
        closeTimer.start()
    }

    Timer { id: closeTimer; interval: 250; onTriggered: agentPanel.visible = false }

    function _buildContext() {
        if (!_veh) return "无飞行器连接。"
        var ctx = "## 飞行器状态\n"
        ctx += "- 固件: " + (_veh.firmwareTypeString || "--") + "\n"
        ctx += "- 类型: " + (_veh.vehicleTypeString || "--") + "\n"
        ctx += "- 解锁: " + (_veh.armed ? "是" : "否") + "\n"
        ctx += "- 模式: " + _modeVal + "\n"
        var bat = _rv(_batF ? _batF.percentRemaining : null, -1)
        if (bat >= 0) ctx += "- 电量: " + bat.toFixed(0) + "%\n"
        if (_batF) {
            ctx += "- 电压: " + _rv(_batF.voltage, 0).toFixed(2) + "V\n"
            ctx += "- 电流: " + _rv(_batF.current, 0).toFixed(1) + "A\n"
        }
        var gpsCnt = _rv(_gpsCntF, 0)
        if (gpsCnt > 0) {
            ctx += "- GPS: " + gpsCnt + "颗 | HDOP:" + _rv(_gpsHdopF, 0).toFixed(2)
            ctx += " | 定位:" + (_rv(_gpsLockF, 0) >= 3 ? "3D" : (_rv(_gpsLockF, 0) >= 2 ? "2D" : "NoLock")) + "\n"
        }
        return ctx
    }

    function cancelRequest() {
        if (_activeXhr) {
            _activeXhr.abort()
            _activeXhr = null
        }
        _isProcessing = false
        _errorMessage = "已取消"
    }

    function sendMessage(msg) {
        if (_isProcessing || !msg.trim()) return
        if (!_apiKey) {
            _errorMessage = "请先在 ⚙ 设置 API Key"
            return
        }
        _errorMessage = ""
        _isProcessing = true
        _history.push({"role": "user", "content": msg})
        chatModel.append({"role": "user", "content": msg})

        var sp = "你是专业无人机调参助手，精通PX4/ArduPilot。简要回答，发现隐患必须警告。中文。\n\n"
        sp += "你可以向飞行器发送指令，在回复中用 <cmd>指令</cmd> 格式。可用指令:\n"
        sp += "  <cmd>takeoff [高度m]</cmd> - 起飞 (默认5m)\n"
        sp += "  <cmd>land</cmd> - 降落\n"
        sp += "  <cmd>rtl</cmd> - 返航\n"
        sp += "  <cmd>pause</cmd> - 悬停\n"
        sp += "  <cmd>alt [+/-]数值</cmd> - 改变高度\n"
        sp += "  <cmd>speed 数值</cmd> - 改变地速m/s\n"
        sp += "  <cmd>goto lat lon [alt]</cmd> - 飞向坐标\n"
        sp += "  <cmd>orbit lat lon alt [半径m]</cmd> - 环绕\n"
        sp += "参数控制指令:\n"
        sp += "  <cmd>paramq 参数名</cmd> - 查询参数当前值\n"
        sp += "  <cmd>paramlist 关键字</cmd> - 列出包含关键字的参数（最多20个）\n"
        sp += "  <cmd>param 参数名 新值</cmd> - 修改参数值（需要用户确认！）\n"
        sp += "参数修改非常危险，仅当用户明确要求且你充分理解参数作用时才建议修改。\n"
        sp += "修改前先解释参数含义和目标值的原因。\n\n"
        sp += _buildContext()
        var msgs = [{"role": "system", "content": sp}]
        var start = Math.max(0, _history.length - 8)
        for (var j = start; j < _history.length; j++) {
            msgs.push(_history[j])
        }

        var xhr = new XMLHttpRequest()
        _activeXhr = xhr
        xhr.open("POST", _apiEndpoint, true)
        xhr.timeout = 15000
        xhr.ontimeout = function() {
            _activeXhr = null
            _isProcessing = false
            _errorMessage = "请求超时"
        }
        xhr.setRequestHeader("Content-Type", "application/json")
        xhr.setRequestHeader("Authorization", "Bearer " + _apiKey)
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE) {
                _activeXhr = null
                _isProcessing = false
                if (xhr.status === 200) {
                    try {
                        var resp = JSON.parse(xhr.responseText)
                        var content = resp.choices[0].message.content
                        _history.push({"role": "assistant", "content": content})
                        chatModel.append({"role": "assistant", "content": content})
                        // Check for embedded commands
                        var cmd = _parseCommands(content)
                        if (cmd && _veh) {
                            // paramq and paramlist are read-only, execute immediately
                            if (cmd.action === "paramq" || cmd.action === "paramlist") {
                                _pendingCmd = cmd
                                _confirmCmd()
                            } else {
                                _pendingCmd = cmd
                                _showConfirm = true
                            }
                        }
                        if (_history.length > 20) {
                            _history = _history.slice(-18)
                        }
                    } catch(e) {
                        _errorMessage = "解析回复失败"
                    }
                } else if (xhr.status !== 0) {
                    _errorMessage = "API 错误: " + xhr.status
                }
                xhr.destroy()
            }
        }
        xhr.send(JSON.stringify({
            "model": _modelName,
            "messages": msgs,
            "stream": false,
            "temperature": 0.3
        }))
    }

    function runQuickCheck(type) {
        var prompts = {
            "battery": "检查电池状态，有异常明确警告。",
            "gps": "检查GPS状态，判断是否满足安全飞行。",
            "vibration": "分析震动水平，震动过高给出排查建议。",
            "preflight": "执行完整飞前检查：电池、GPS、传感器、遥控器、螺旋桨、安全开关。",
            "params": "检查飞控参数异常。"
        }
        sendMessage(prompts[type] || type)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: showSettings ? "API 设置" : "AI 调试助手"
                font.pixelSize: 14
                font.bold: true
                color: "#ffffff"
                Layout.fillWidth: true
            }

            Button {
                text: showSettings ? "←返回" : "⚙"
                flat: true
                onClicked: showSettings = !showSettings
                contentItem: Text {
                    text: parent.text
                    color: "#aaaacc"
                    font.pixelSize: 12
                }
                background: Rectangle { color: "transparent" }
            }

            Button {
                text: "✕"
                flat: true
                onClicked: agentPanel.close()
                contentItem: Text {
                    text: parent.text
                    color: "#aaaacc"
                    font.pixelSize: 14
                }
                background: Rectangle { color: "transparent" }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#334455"
        }

        // ---- Settings view ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: showSettings
            spacing: 6

            Label {
                text: "API Key（必填）"
                font.pixelSize: 11
                color: "#aaaacc"
            }

            TextField {
                id: apiKeyField
                Layout.fillWidth: true
                font.pixelSize: 12
                text: _apiKey
                echoMode: TextInput.Password
                placeholderText: "sk-xxxxxxxxxxxxxxxx"
                onTextChanged: {
                    _apiKey = text
                    _saveConfig()
                }
                background: Rectangle {
                    radius: 4
                    color: "#16213e"
                    border.color: "#334455"
                    border.width: 1
                }
                color: "#ffffff"
            }

            Button {
                Layout.alignment: Qt.AlignRight
                flat: true
                text: apiKeyField.echoMode === TextInput.Normal ? "隐藏" : "显示"
                onClicked: {
                    apiKeyField.echoMode = apiKeyField.echoMode === TextInput.Normal
                            ? TextInput.Password : TextInput.Normal
                }
                contentItem: Text {
                    text: parent.text
                    color: "#4A90D9"
                    font.pixelSize: 10
                }
                background: Rectangle { color: "transparent" }
            }

            Label {
                text: "API Endpoint"
                font.pixelSize: 11
                color: "#aaaacc"
            }

            TextField {
                Layout.fillWidth: true
                font.pixelSize: 11
                text: _apiEndpoint
                placeholderText: "https://dashscope.aliyuncs.com/..."
                onTextChanged: {
                    _apiEndpoint = text
                    _saveConfig()
                }
                background: Rectangle {
                    radius: 4
                    color: "#16213e"
                    border.color: "#334455"
                    border.width: 1
                }
                color: "#ffffff"
            }

            Label {
                text: "Model"
                font.pixelSize: 11
                color: "#aaaacc"
            }

            ComboBox {
                Layout.fillWidth: true
                model: ["qwen-plus", "qwen-max", "deepseek-chat", "deepseek-reasoner", "glm-4", "gpt-4o"]
                currentIndex: {
                    for (var i = 0; i < model.length; i++) {
                        if (model[i] === _modelName) return i
                    }
                    return 3
                }
                onCurrentTextChanged: {
                    _modelName = currentText
                    _saveConfig()
                }
                background: Rectangle {
                    radius: 4
                    color: "#16213e"
                    border.color: "#334455"
                    border.width: 1
                }
                contentItem: Text {
                    text: parent.displayText
                    color: "#ffffff"
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                }
            }

            Item { Layout.fillHeight: true }

            Button {
                Layout.fillWidth: true
                text: "保存并返回"
                onClicked: showSettings = false
                background: Rectangle {
                    radius: 5
                    color: "#4A90D9"
                }
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Label {
                Layout.fillWidth: true
                font.pixelSize: 9
                color: _settingsStatus === "已加载" || _settingsStatus === "已保存" ? "#44ff44" : "#ff4444"
                wrapMode: Text.Wrap
                text: "支持 OpenAI 兼容 API\n通义千问 · DeepSeek · 智谱GLM\n状态: " + _settingsStatus
            }
        }

        // ---- Status bar ----
        Rectangle {
            Layout.fillWidth: true
            height: 28
            radius: 4
            visible: !showSettings
            color: _statusConnected ? "#0a2a1a" : "#2a1a1a"
            border {
                color: _statusConnected ? "#2ECC71" : "#ff4444"
                width: 1
            }

            RowLayout {
                anchors {
                    fill: parent
                    leftMargin: 8
                    rightMargin: 8
                }

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: _statusConnected ? "#2ECC71" : "#ff4444"
                }

                Text {
                    text: _statusConnected
                            ? "OK · " + _statusMode + " · 电" + _statusBat + "% · GPS " + _statusGps
                            : "无飞行器连接 · " + _settingsStatus
                    color: _statusConnected ? "#88ffaa" : "#ff8888"
                    font.pixelSize: 10
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
        }

        // ---- Quick buttons ----
        RowLayout {
            Layout.fillWidth: true
            visible: !showSettings
            spacing: 3

            Repeater {
                model: [
                    { label: "飞前检查", type: "preflight" },
                    { label: "电池", type: "battery" },
                    { label: "GPS", type: "gps" },
                    { label: "震动", type: "vibration" },
                    { label: "参数", type: "params" }
                ]
                delegate: Button {
                    text: modelData.label
                    flat: true
                    enabled: !_isProcessing
                    onClicked: runQuickCheck(modelData.type)
                    background: Rectangle {
                        radius: 3
                        color: "#16213e"
                        border {
                            color: "#334455"
                            width: 1
                        }
                    }
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 10
                        color: parent.enabled ? "#aaaacc" : "#445566"
                    }
                }
            }
        }

        // ---- Chat list ----
        ListView {
            id: chatView
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !showSettings
            clip: true
            spacing: 4
            model: ListModel { id: chatModel }

            delegate: Row {
                spacing: 4
                anchors.right: role === "user" ? parent.right : undefined

                Rectangle {
                    width: 22
                    height: 22
                    radius: 11
                    color: role === "user" ? "#4A90D9" : "#2ECC71"
                    Text {
                        anchors.centerIn: parent
                        text: role === "user" ? "你" : "AI"
                        color: "white"
                        font.pixelSize: 9
                    }
                }

                Rectangle {
                    width: Math.min(contentLabel.implicitWidth + 10, chatView.width * 0.72)
                    height: contentLabel.implicitHeight + 10
                    radius: 5
                    color: role === "user" ? "#4A90D9" : "#16213e"

                    Text {
                        id: contentLabel
                        anchors {
                            fill: parent
                            margins: 5
                        }
                        text: content
                        color: role === "user" ? "white" : "#ccccee"
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                }
            }

            onCountChanged: positionViewAtEnd()
        }

        // ---- Error / processing ----
        Label {
            Layout.fillWidth: true
            font.pixelSize: 10
            text: _isProcessing ? "思考中..." : _errorMessage
            color: _errorMessage ? "#ff4444" : "#556677"
            visible: !showSettings && (_isProcessing || _errorMessage.length > 0)
            wrapMode: Text.Wrap
        }

        // ---- Input row ----
        RowLayout {
            Layout.fillWidth: true
            visible: !showSettings
            spacing: 4

            TextField {
                id: inputField
                Layout.fillWidth: true
                font.pixelSize: 12
                placeholderText: "输入问题..."
                enabled: !_isProcessing
                onAccepted: sendButton.clicked()
                background: Rectangle {
                    radius: 4
                    color: "#16213e"
                    border.color: "#334455"
                    border.width: 1
                }
                color: "#ffffff"
            }

            Button {
                id: sendButton
                text: _isProcessing ? "■" : "▶"
                font.pixelSize: 14
                enabled: _isProcessing || inputField.text.trim().length > 0
                onClicked: {
                    if (_isProcessing) {
                        cancelRequest()
                    } else {
                        sendMessage(inputField.text.trim())
                        inputField.text = ""
                    }
                }
                background: Rectangle {
                    radius: 4
                    color: _isProcessing ? "#ff4444" : (sendButton.enabled ? "#4A90D9" : "#1a2a4a")
                }
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        // ── Command Confirmation Popup ──
        Rectangle {
            anchors.fill: parent
            color: "#cc000000"
            visible: _showConfirm
            z: 100

            MouseArea {
                anchors.fill: parent
                // block clicks through
            }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 300)
                height: cmdCol.implicitHeight + 40
                radius: 10
                color: "#1a2a4a"
                border { color: "#ffaa00"; width: 1.5 }

                Column {
                    id: cmdCol
                    anchors { fill: parent; margins: 20 }
                    spacing: 12

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "⚠ AI 请求执行指令"
                        color: "#ffaa00"
                        font.pixelSize: 14
                        font.bold: true
                    }

                    Rectangle {
                        width: parent.width; height: 1; color: "#334455"
                    }

                    Text {
                        width: parent.width
                        text: _pendingCmd ? _describeCmd(_pendingCmd) : ""
                        color: "#ffffff"
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Text {
                        width: parent.width
                        text: "请确认是否执行此操作"
                        color: "#888899"
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                    }

                    RowLayout {
                        width: parent.width
                        spacing: 10

                        Button {
                            Layout.fillWidth: true
                            text: "✗ 取消"
                            onClicked: _cancelCmd()
                            background: Rectangle {
                                radius: 5; color: "#334455"
                                border { color: "#556677"; width: 1 }
                            }
                            contentItem: Text {
                                text: parent.text; color: "#aaaacc"
                                font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter
                            }
                        }

                        Button {
                            Layout.fillWidth: true
                            text: "✓ 确认执行"
                            onClicked: _confirmCmd()
                            background: Rectangle {
                                radius: 5; color: "#33ff4444"
                                border { color: "#ff6666"; width: 1.5 }
                            }
                            contentItem: Text {
                                text: parent.text; color: "#ff6666"
                                font.pixelSize: 13; font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                }
            }
        }
    }
}
