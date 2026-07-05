import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QGroundControl

Rectangle {
    id: agentPanel
    width: Math.min(_sp * 40, parent.width * 0.78)
    height: parent.height
    color: qgcPal.window
    visible: false
    x: parent.width

    // ═══ File picker dialog ═══
    FileDialog {
        id: logFileDialog
        title: "选择飞行日志"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Flight logs (*.ulg *.bin)", "All files (*)"]
        onAccepted: { runLogFile(selectedFile); }
    }

    // ═══ Theme & scaling ═══
    readonly property var qgcPal: QGroundControl.globalPalette
    readonly property real _sp: _measureText.fontPixelSize
    Text {
        id: _measureText
        text: "X"
        font.pointSize: 10
        readonly property real fontPixelSize: Math.max(Math.round(contentHeight), 8)
    }

    // ── JARVIS-inspired color tokens (adapted to QGC palette) ──
    readonly property color _accentCyan:    qgcPal.primaryButton
    readonly property color _accentOrange:  qgcPal.colorOrange
    readonly property color _accentGreen:   qgcPal.colorGreen
    readonly property color _accentRed:     qgcPal.colorRed
    readonly property color _accentBlue:    qgcPal.colorBlue
    readonly property color _bgDark:        qgcPal.windowShadeDark
    readonly property color _bgCard:        qgcPal.windowShade
    readonly property color _borderDim:     qgcPal.groupBorder
    readonly property color _textPrimary:   qgcPal.text
    readonly property color _textDim:       Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.5)

    property bool isExpanded: false
    property int currentTab: 0  // 0=chat, 1=diagnostics, 2=charts, 3=settings, 4=knowledge

    // ═══ AgentManager (injected by FlyViewCustomLayer) ═══
    property var _agentMgr: null
    readonly property bool _agentOK: _agentMgr !== null
    readonly property bool _agentAuto: _agentOK ? _agentMgr.isAutonomous : false
    readonly property bool _agentBusy: _agentOK ? _agentMgr.isProcessing : false
    readonly property string _agentErr: _agentOK ? (_agentMgr.errorMessage || "") : ""
    property string _dismissedError: ""

    // ═══ Panel open/close animation ═══
    Behavior on x {
        NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
    }
    Behavior on opacity {
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
    }

    function open() {
        visible = true
        isExpanded = true
        opacity = 1.0
        x = parent.width - width
    }
    function close() {
        isExpanded = false
        opacity = 0
        x = parent.width
        closeTimer.start()
    }
    Timer { id: closeTimer; interval: 260; onTriggered: agentPanel.visible = false }

    // ═══ Vehicle data bindings ═══
    readonly property var _veh: QGroundControl.multiVehicleManager.activeVehicle
    readonly property string _modeVal: _veh ? _veh.flightMode : "--"
    readonly property var _batF: _veh && _veh.batteries && _veh.batteries.count > 0 ? _veh.batteries.get(0) : null
    readonly property var _gpsCntF: _veh && _veh.gps ? _veh.gps.count : null
    function _rv(fact, def) { try { return fact ? fact.rawValue : def } catch(e) { return def } }

    readonly property bool _statusConnected: _veh !== null
    readonly property string _statusMode: _veh ? _modeVal : "--"
    readonly property string _statusBat: {
        var bat = _rv(_batF ? _batF.percentRemaining : null, -1)
        return bat >= 0 ? bat.toFixed(0) : "?"
    }
    readonly property string _statusGps: {
        var gps = _rv(_gpsCntF, 0)
        return gps > 0 ? String(gps) : "?"
    }

    // ═══ Vision capture ═══
    function _onVisionCapture(base64data) {
        if (!base64data) return
        var ag = _agentMgr
        if (ag && typeof ag.sendVisionQuery === 'function') {
            ag.sendVisionQuery("分析图像内容，描述画面中的物体、障碍物、地形特征和任何异常情况。", base64data)
        }
    }
    function _startVisionCapture() {
        var layer = agentPanel.parent
        while (layer && !layer.videoControl) {
            layer = layer.parent
        }
        if (layer && layer._camMa) {
            layer._camMa.clicked(Qt.LeftButton)
        }
    }

    // ═══ Provider presets ═══
    readonly property var _providerPresets: ({
        "DeepSeek":                { baseUrl: "https://api.deepseek.com/v1",                     defaultModel: "deepseek-chat",      keyHint: "sk-xxxxxxxxxxxxxxxx" },
        "DashScope (通义千问)":    { baseUrl: "https://dashscope.aliyuncs.com/compatible-mode/v1", defaultModel: "qwen-plus",           keyHint: "sk-xxxxxxxxxxxxxxxx" },
        "智谱GLM":                 { baseUrl: "https://open.bigmodel.cn/api/paas/v4",             defaultModel: "glm-4-flash",         keyHint: "xxxxxxxxxxxxxxxx.xxxxxx" },
        "OpenAI":                  { baseUrl: "https://api.openai.com/v1",                        defaultModel: "gpt-4o-mini",         keyHint: "sk-xxxxxxxxxxxxxxxx" },
        "Ollama (本地)":           { baseUrl: "http://localhost:11434/v1",                        defaultModel: "llama3",              keyHint: "ollama (无需Key)" },
        "自定义":                  { baseUrl: "",                                                  defaultModel: "",                    keyHint: "" }
    })

    function _detectProviderIndex() {
        if (!_agentOK) return 0
        var base = _agentMgr.baseApiEndpoint()
        var names = Object.keys(_providerPresets)
        for (var i = 0; i < names.length - 1; i++) { // skip "自定义"
            if (_providerPresets[names[i]].baseUrl === base) return i
        }
        return names.length - 1 // "自定义"
    }

    function _applyProviderPreset(index) {
        var names = Object.keys(_providerPresets)
        if (index < 0 || index >= names.length) return
        var name = names[index]
        var preset = _providerPresets[name]
        if (preset.baseUrl.length > 0) {
            baseUrlField.text = preset.baseUrl
            if (_agentMgr) _agentMgr.setApiEndpoint(preset.baseUrl)
        }
        if (preset.defaultModel.length > 0) {
            modelCombo.editText = preset.defaultModel
            if (_agentMgr) _agentMgr.setModelName(preset.defaultModel)
        }
        if (preset.keyHint.length > 0) {
            apiKeyField.placeholderText = preset.keyHint
        }
        // Auto-fetch models when provider changes (if key is set)
        if (_agentMgr && baseUrlField.text.length > 0 && apiKeyField.text.length > 0) {
            _agentMgr.setApiKey(apiKeyField.text)
            _agentMgr.setApiEndpoint(baseUrlField.text)
            _agentMgr.fetchModels()
        }
    }

    // ═══ Clipboard ═══
    function _copyToClipboard(text) {
        if (!text || text.length === 0) return
        var te = Qt.createQmlObject('import QtQuick; TextEdit { visible: false }', agentPanel, "_clipHelper")
        try {
            te.text = text
            te.selectAll()
            te.copy()
        } finally {
            te.destroy()
        }
        chatModel.append({"role": "system", "content": "已复制到剪贴板", "ts": ""})
    }

    // ═══ Pending command confirmation ═══
    property var _pendingCmd: null
    property bool _showConfirm: false
    property string _pendingDesc: ""
    property int _confirmSecondsLeft: 30
    property var _fetchedModels: []
    property int _thinkingItemIndex: -1
    property int _streamingItemIndex: -1

    function _syncThinkingItem() {
        if (!_agentMgr) return
        var txt = _agentMgr.thinkingText || ""
        var busy = _agentMgr.isProcessing
        if (!busy && txt.length === 0) { _removeThinkingItems(); return }
        if (txt.length > 0) {
            if (_thinkingItemIndex < 0 || _thinkingItemIndex >= chatModel.count
                || chatModel.get(_thinkingItemIndex).role !== "thinking") {
                _removeThinkingItems()
                chatModel.append({"role": "thinking", "content": txt, "ts": "", "_isCurrentThinking": busy})
                _thinkingItemIndex = chatModel.count - 1
            } else {
                chatModel.setProperty(_thinkingItemIndex, "content", txt)
                chatModel.setProperty(_thinkingItemIndex, "_isCurrentThinking", busy)
            }
        } else if (busy && _thinkingItemIndex < 0) {
            _removeThinkingItems()
            chatModel.append({"role": "thinking", "content": "", "ts": "", "_isCurrentThinking": true})
            _thinkingItemIndex = chatModel.count - 1
        }
    }
    function _removeThinkingItems() {
        for (var i = chatModel.count - 1; i >= 0; i--) {
            if (chatModel.get(i).role === "thinking") chatModel.remove(i)
        }
        _thinkingItemIndex = -1
    }

    // Sync streaming answer text (real-time partial response)
    function _syncStreamingItem() {
        if (!_agentMgr) return
        var txt = _agentMgr.streamingText || ""
        var busy = _agentMgr.isProcessing
        if (!busy && txt.length === 0) { _removeStreamingItem(); return }
        if (txt.length > 0) {
            if (_streamingItemIndex < 0 || _streamingItemIndex >= chatModel.count
                || chatModel.get(_streamingItemIndex).role !== "streaming") {
                _removeStreamingItem()
                chatModel.append({"role": "streaming", "content": txt, "ts": ""})
                _streamingItemIndex = chatModel.count - 1
            } else {
                chatModel.setProperty(_streamingItemIndex, "content", txt)
            }
        }
    }
    function _removeStreamingItem() {
        for (var i = chatModel.count - 1; i >= 0; i--) {
            if (chatModel.get(i).role === "streaming") chatModel.remove(i)
        }
        _streamingItemIndex = -1
    }

    // 30s auto-reject timer
    Timer {
        id: confirmTimeout
        interval: 1000; repeat: true; running: _showConfirm
        onTriggered: {
            _confirmSecondsLeft--
            if (_confirmSecondsLeft <= 0) {
                _showConfirm = false; _pendingCmd = null
                if (_agentMgr) _agentMgr.rejectCommand()
                chatModel.append({"role": "system", "content": "⏰ 确认超时，指令自动取消", "ts": ""})
            }
        }
    }
    function _showUnreadyWarning() {
        if (!_agentMgr) {
            chatModel.append({"role": "system", "content": "⚠ AgentManager 未就绪，请检查 C++ 模块是否正确加载", "ts": ""})
        }
    }

    // ═══ Log file handling ═══
    property string _pendingLogPath: ""
    property bool _expectingLogPath: false
    property var _logListCache: []
    property bool _showLogPicker: false
    property var _onboardLogList: []
    function runLogFile(path) {
        if (!path || path.trim().length === 0) return
        _pendingLogPath = path
        chatModel.append({"role": "system", "content": "📂 " + path + "\n分析类型: 全部 (振动+GPS+电池+EKF)", "ts": ""})
        var ag = _agentMgr
        if (ag && typeof ag.runLogAnalysis === 'function') ag.runLogAnalysis(path, "all")
        else chatModel.append({"role": "system", "content": "⚠ runLogAnalysis 不可用", "ts": ""})
    }

    // ═══ Timestamp helper ═══
    function _nowTs() {
        var d = new Date()
        return d.getHours().toString().padStart(2, '0') + ":" +
               d.getMinutes().toString().padStart(2, '0')
    }

    // ═══ Send message or cancel request ═══
    function _doSend() {
        var ag = _agentMgr
        if (!ag) { _showUnreadyWarning(); return }
        var txt = inputField.text.trim()
        if (ag.isProcessing) { ag.cancelRequest() }
        else if (/^\/download\s+(\d+)$/i.test(txt)) {
            var idx = parseInt(txt.match(/^\/download\s+(\d+)$/i)[1])
            if (typeof ag.downloadOnboardLogByIndex === 'function') ag.downloadOnboardLogByIndex(idx)
            inputField.text = ""
        }
        else if (_expectingLogPath || /\.(ulg|px4log|bin)$/i.test(txt)) {
            _expectingLogPath = false; runLogFile(txt); inputField.text = ""
            inputField.placeholderText = _agentOK && _agentAuto ? "自主模式中..." : "输入问题或任务..."
        } else { ag.sendMessage(txt); inputField.text = "" }
    }

    // ═══ Drag-drop file support ═══
    DropArea {
        anchors.fill: parent
        z: QGroundControl.zOrderTopMost + 10
        keys: ["text/uri-list"]
        onEntered: function(drag) { drag.accept(); _dragOver = true }
        onExited: { _dragOver = false }
        onDropped: function(drop) {
            _dragOver = false
            if (drop.hasUrls && drop.urls.length > 0) {
                var path = drop.urls[0].toString()
                if (path.startsWith("file://")) path = path.substring(7)
                runLogFile(path)
            }
        }
    }
    property bool _dragOver: false
    Rectangle {
        anchors.fill: parent; z: QGroundControl.zOrderTopMost + 9
        color: Qt.rgba(qgcPal.buttonHighlight.r, qgcPal.buttonHighlight.g, qgcPal.buttonHighlight.b, 0.2)
        border { color: qgcPal.buttonHighlight; width: 2 }
        visible: _dragOver
        Text {
            anchors.centerIn: parent
            text: "📂 拖放日志文件到此处分析"
            color: qgcPal.buttonHighlightText
            font.pixelSize: Math.round(_sp * 1.5)
            font.weight: Font.Bold
        }
    }

    // ═══ Prevent click-through ═══
    MouseArea { anchors.fill: parent; z: -1 }

    // ════════════════════════════════════════════════════
    //  Main Layout
    // ════════════════════════════════════════════════════
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Math.round(_sp * 0.6)
        spacing: Math.round(_sp * 0.35)

        // ════ HEADER ════
        RowLayout {
            Layout.fillWidth: true
            spacing: Math.round(_sp * 0.5)

            Text {
                text: "AI 助手"
                font.pixelSize: Math.round(_sp * 1.5)
                font.weight: Font.Bold
                color: _accentCyan
                Layout.fillWidth: true
            }

            // Connection indicator
            Rectangle {
                width: Math.round(_sp * 0.7); height: width; radius: width / 2
                color: _statusConnected ? _accentGreen : _accentRed
                Layout.alignment: Qt.AlignVCenter
                SequentialAnimation on opacity {
                    running: _agentBusy; loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 0.3; duration: 600 }
                    NumberAnimation { from: 0.3; to: 1; duration: 600 }
                }
            }

            // Close
            Rectangle {
                width: btnX.implicitWidth + _sp * 1.2
                height: Math.round(_sp * 2.2)
                radius: Math.round(_sp * 0.3)
                color: closeHover.hovered ? Qt.rgba(qgcPal.buttonHighlight.r, qgcPal.buttonHighlight.g, qgcPal.buttonHighlight.b, 0.2) : "transparent"
                Text {
                    id: btnX; anchors.centerIn: parent
                    text: "✕"; color: qgcPal.buttonText
                    font.pixelSize: Math.round(_sp * 1.3)
                }
                MouseArea {
                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: agentPanel.close()
                }
                HoverHandler { id: closeHover }
            }
        }

        // ── Divider ──
        Rectangle { Layout.fillWidth: true; height: 1; color: _borderDim }

        // ════ TAB BAR (borrowed from JARVIS) ════
        RowLayout {
            Layout.fillWidth: true; spacing: Math.round(_sp * 0.3)

            Repeater {
                model: [
                    { label: "◈ 对话", icon: "对话" },
                    { label: "◉ 诊断", icon: "诊断" },
                    { label: "📊 图表", icon: "图表" },
                    { label: "⚙ 设置", icon: "设置" },
                    { label: "📚 知识库", icon: "知识库" },
                    { label: "🗄 归档", icon: "归档" }
                ]
                Rectangle {
                    Layout.fillWidth: true; height: Math.round(_sp * 2.4); radius: Math.round(_sp * 0.3)
                    color: currentTab === index ? Qt.rgba(qgcPal.primaryButton.r, qgcPal.primaryButton.g, qgcPal.primaryButton.b, 0.12) : "transparent"
                    border.color: currentTab === index ? _accentCyan : _borderDim
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        color: currentTab === index ? _accentCyan : _textDim
                        font { pixelSize: _sp; bold: true }
                    }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: currentTab = index
                    }
                }
            }
        }

        // ── Status Bar ──
        Rectangle {
            Layout.fillWidth: true
            height: Math.round(_sp * 3.0)
            radius: Math.round(_sp * 0.35)
            color: _statusConnected ? Qt.rgba(_accentGreen.r, _accentGreen.g, _accentGreen.b, 0.08)
                                   : Qt.rgba(_accentRed.r, _accentRed.g, _accentRed.b, 0.08)
            border { color: _statusConnected ? _accentGreen : _accentRed; width: 1 }

            RowLayout {
                anchors { fill: parent; leftMargin: Math.round(_sp * 0.8); rightMargin: Math.round(_sp * 0.8) }
                spacing: Math.round(_sp * 0.5)

                Rectangle {
                    width: Math.round(_sp * 0.6); height: width; radius: width / 2
                    color: _agentBusy ? _accentOrange :
                           _statusConnected ? _accentGreen : _accentRed
                    SequentialAnimation on opacity {
                        running: _agentBusy; loops: Animation.Infinite
                        NumberAnimation { from: 1; to: 0.2; duration: 500 }
                        NumberAnimation { from: 0.2; to: 1; duration: 500 }
                    }
                }
                Text {
                    text: _agentBusy ? "处理中..." :
                          _statusConnected ? "OK · " + _statusMode + " · 电" + _statusBat + "% · GPS " + _statusGps
                                           : "无飞行器连接"
                    color: _statusConnected ? _accentGreen : _accentRed
                    font.pixelSize: Math.round(_sp * 1.0)
                    font.weight: Font.Medium
                    Layout.fillWidth: true; elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // ════ TAB CONTENT ════
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            currentIndex: currentTab

            // ──────────── TAB 0: CHAT ────────────
            ColumnLayout {
                spacing: Math.round(_sp * 0.35)

                // ═══ Quick Buttons ═══
                Flow {
                    Layout.fillWidth: true
                    spacing: Math.round(_sp * 0.25)

                    Repeater {
                        model: [
                            { label: "飞前检查", type: "preflight" },
                            { label: "电池", type: "battery" },
                            { label: "GPS", type: "gps" },
                            { label: "参数", type: "params" },
                            { label: "截图", type: "vision" },
                            { label: "📂 日志", type: "filepicker" },
                            { label: "机载日志", type: "onboard" },
                            { label: "📋 历史", type: "history" }
                        ]
                        delegate: Rectangle {
                            width: chipLbl.implicitWidth + Math.round(_sp * 1.4)
                            height: Math.round(_sp * 2.2)
                            radius: height / 2
                            color: qbEnabled ? (qbHover.hovered ? Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.12) : Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.05)) : _bgDark
                            border { color: qbEnabled ? (qbHover.hovered ? _accentCyan : Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.25)) : _borderDim; width: 1 }
                            property bool qbEnabled: modelData.type === "vision" ? _agentOK : (_agentOK && !_agentBusy)
                            Text {
                                id: chipLbl; anchors.centerIn: parent; text: modelData.label
                                color: parent.qbEnabled ? _textPrimary : _textDim
                                font.pixelSize: Math.round(_sp * 0.9)
                            }
                            MouseArea {
                                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                enabled: parent.qbEnabled
                                onClicked: {
                                    if (modelData.type === "vision") { _startVisionCapture() }
                                    else if (modelData.type === "filepicker") { logFileDialog.open() }
                                    else if (modelData.type === "history") {
                                        var agH = _agentMgr
                                        if (agH && typeof agH.listDownloadedLogs === 'function') agH.listDownloadedLogs()
                                        else _showUnreadyWarning()
                                    } else if (modelData.type === "onboard") {
                                        var ag = _agentMgr
                                        if (!ag) { _showUnreadyWarning(); return }
                                        // If we already have the list cached, show picker directly
                                        var cached = typeof ag.listOnboardLogs === 'function' ? ag.listOnboardLogs() : []
                                        if (cached && cached.length > 0) {
                                            _onboardLogList = cached
                                            _showLogPicker = true
                                        } else {
                                            ag.downloadOnboardLogs()
                                        }
                                    } else {
                                        var ag2 = _agentMgr
                                        if (ag2) ag2.runQuickCheck(modelData.type)
                                    }
                                }
                            }
                            HoverHandler { id: qbHover; enabled: parent.qbEnabled }
                        }
                    }
                }

                // ═══ Chat List (ChatGPT-style flat) ═══
                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    color: "transparent"; clip: true

                    ListView {
                        id: chatView
                        anchors.fill: parent; anchors.margins: Math.round(_sp * 0.5)
                        model: ListModel { id: chatModel }
                        spacing: Math.round(_sp * 0.35); clip: true

                        // Auto-scroll to bottom when new messages arrive (not on content updates)
                        onCountChanged: Qt.callLater(function() { positionViewAtEnd() })

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AlwaysOn
                            contentItem: Rectangle {
                                implicitWidth: Math.round(_sp * 0.3); radius: Math.round(_sp * 0.15)
                                color: parent.pressed ? _accentCyan : _borderDim
                            }
                            background: Rectangle { implicitWidth: Math.round(_sp * 0.3); color: "transparent" }
                        }

                        add: Transition {
                            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 }
                        }

                        delegate: Item {
                            id: msgDelegate
                            width: chatView.width
                            height: msgCol.height + Math.round(_sp * 0.6)

                            readonly property real _maxContentWidth: chatView.width - Math.round(_sp * 3.6)
                            readonly property bool _isUser: model.role === "user"
                            readonly property bool _isThinking: model.role === "thinking"
                            readonly property bool _isSystem: model.role === "system"
                            readonly property color _roleColor: _isUser ? _accentOrange
                                                               : _isThinking ? _accentBlue
                                                               : _isSystem ? _accentGreen
                                                               : _accentCyan
                            readonly property string _avatarText: _isUser ? "我"
                                                                : _isThinking ? "💭"
                                                                : _isSystem ? "⚙"
                                                                : "AI"

                            ColumnLayout {
                                id: msgCol
                                width: parent.width
                                anchors.top: parent.top
                                spacing: Math.round(_sp * 0.2)

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Math.round(_sp * 0.4)
                                    layoutDirection: msgDelegate._isUser ? Qt.RightToLeft : Qt.LeftToRight

                                    // Avatar
                                    Rectangle {
                                        Layout.preferredWidth: Math.round(_sp * 2.2)
                                        Layout.preferredHeight: Math.round(_sp * 2.2)
                                        Layout.alignment: Qt.AlignTop
                                        radius: width / 2
                                        color: Qt.rgba(msgDelegate._roleColor.r, msgDelegate._roleColor.g, msgDelegate._roleColor.b, 0.15)
                                        border.color: msgDelegate._roleColor; border.width: 1
                                        Text {
                                            anchors.centerIn: parent
                                            text: msgDelegate._avatarText
                                            color: msgDelegate._roleColor
                                            font.pixelSize: Math.round(_sp * 0.75)
                                            font.bold: true
                                        }
                                    }

                                    // Content column
                                    ColumnLayout {
                                        Layout.maximumWidth: msgDelegate._maxContentWidth
                                        Layout.fillWidth: !msgDelegate._isUser
                                        spacing: Math.round(_sp * 0.15)

                                        // Name + timestamp (hidden for user)
                                        RowLayout {
                                            spacing: Math.round(_sp * 0.3)
                                            visible: !msgDelegate._isUser
                                            Text {
                                                text: msgDelegate._isThinking ? "思考过程"
                                                    : msgDelegate._isSystem ? "系统消息"
                                                    : "AI 助手"
                                                color: msgDelegate._roleColor
                                                font { pixelSize: Math.round(_sp * 0.75); bold: true }
                                            }
                                            Text {
                                                text: model.ts || ""
                                                color: _textDim
                                                font.pixelSize: Math.round(_sp * 0.65)
                                                visible: text.length > 0
                                            }
                                        }

                                        // Bubble (user: tinted, right-aligned) / flat text (AI: transparent)
                                        Rectangle {
                                            id: bubble
                                            Layout.alignment: msgDelegate._isUser ? Qt.AlignRight : Qt.AlignLeft
                                            Layout.fillWidth: !msgDelegate._isUser
                                            Layout.preferredWidth: msgDelegate._isUser
                                                ? Math.min(msgText.implicitWidth + Math.round(_sp * 1.0), msgDelegate._maxContentWidth)
                                                : -1
                                            Layout.preferredHeight: msgText.contentHeight + Math.round(_sp * 0.9)
                                            color: msgDelegate._isUser
                                                ? Qt.rgba(msgDelegate._roleColor.r, msgDelegate._roleColor.g, msgDelegate._roleColor.b, 0.12)
                                                : "transparent"
                                            radius: Math.round(_sp * 0.5)

                                            Text {
                                                id: msgText
                                                anchors { fill: parent; margins: Math.round(_sp * 0.5) }
                                                text: msgDelegate._isThinking
                                                    ? (typeof model._isCurrentThinking !== "undefined" && model._isCurrentThinking
                                                       ? "▾ 思考过程\n" + (model.content || "")
                                                       : "▸ 思考过程（点击展开）")
                                                    : model.role === "streaming"
                                                    ? (model.content || "") + "▌"
                                                    : (model.content || "")
                                                color: msgDelegate._isUser ? qgcPal.buttonText
                                                     : msgDelegate._isThinking ? msgDelegate._roleColor
                                                     : _textPrimary
                                                font.pixelSize: msgDelegate._isThinking ? Math.round(_sp * 0.9) : Math.round(_sp * 1.05)
                                                wrapMode: Text.Wrap
                                                textFormat: msgDelegate._isThinking
                                                    ? Text.PlainText
                                                    : model.role === "streaming"
                                                    ? Text.PlainText  // Avoid expensive Markdown re-parse on every token
                                                    : Text.MarkdownText
                                                lineHeight: 1.35
                                            }

                                            MouseArea {
                                                anchors.fill: parent; visible: msgDelegate._isThinking
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    var cur = typeof model._isCurrentThinking !== "undefined" ? model._isCurrentThinking : false
                                                    chatModel.setProperty(index, "_isCurrentThinking", !cur)
                                                }
                                            }
                                        }

                                        // Action row (copy) — below messages with content
                                        RowLayout {
                                            spacing: Math.round(_sp * 0.2)
                                            visible: !msgDelegate._isThinking && (model.content || "").length > 0
                                            layoutDirection: msgDelegate._isUser ? Qt.RightToLeft : Qt.LeftToRight
                                            Rectangle {
                                                width: actCopyLbl.implicitWidth + Math.round(_sp * 0.9)
                                                height: Math.round(_sp * 1.6)
                                                radius: height / 2
                                                color: actCopyHover.hovered ? Qt.rgba(msgDelegate._roleColor.r, msgDelegate._roleColor.g, msgDelegate._roleColor.b, 0.12) : "transparent"
                                                Text {
                                                    id: actCopyLbl; anchors.centerIn: parent
                                                    text: "📋 复制"; color: _textDim
                                                    font.pixelSize: Math.round(_sp * 0.7)
                                                }
                                                MouseArea {
                                                    id: actCopyHover
                                                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                                    onClicked: _copyToClipboard(model.content || "")
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Empty state (ChatGPT-style greeting)
                        Column {
                            anchors.centerIn: parent; visible: chatView.count === 0; spacing: Math.round(_sp * 0.6)
                            Text {
                                text: "✈"
                                color: _accentCyan; font.pixelSize: Math.round(_sp * 2.4)
                                horizontalAlignment: Text.AlignHCenter
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Text {
                                text: "有什么可以帮你？"
                                color: _textPrimary; font { pixelSize: Math.round(_sp * 1.4); bold: true }
                                horizontalAlignment: Text.AlignHCenter
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Text {
                                text: "飞行诊断 · 日志分析 · 参数调整 · 任务规划"
                                color: _textDim; font.pixelSize: Math.round(_sp * 0.95)
                                horizontalAlignment: Text.AlignHCenter
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }
                    }
                }

                // ═══ Processing animation bar (JARVIS-style) ═══
                Rectangle {
                    Layout.fillWidth: true; height: Math.round(_sp * 0.15)
                    color: _bgCard; radius: Math.round(_sp * 0.07)
                    visible: _agentBusy
                    Rectangle {
                        id: procBar
                        width: parent.width * 0.25; height: parent.height; radius: parent.radius
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: "transparent" }
                            GradientStop { position: 0.5; color: _accentCyan }
                            GradientStop { position: 1; color: "transparent" }
                        }
                        SequentialAnimation on x { loops: Animation.Infinite
                            NumberAnimation { from: -procBar.width; to: procBar.parent.width; duration: 1500; easing.type: Easing.InOutQuad }
                        }
                    }
                }

                // ═══ Error Bar ═══
                Rectangle {
                    Layout.fillWidth: true
                    height: (_agentErr.length > 0 && _agentErr !== _dismissedError) ? Math.round(_sp * 2.5) : 0
                    visible: _agentErr.length > 0 && _agentErr !== _dismissedError
                    radius: Math.round(_sp * 0.3)
                    color: Qt.rgba(_accentRed.r, _accentRed.g, _accentRed.b, 0.08)
                    border { color: _accentRed; width: 1 }

                    RowLayout {
                        anchors { fill: parent; leftMargin: Math.round(_sp * 0.6); rightMargin: Math.round(_sp * 0.6) }
                        Text {
                            text: "⚠ " + _agentErr; color: _accentRed
                            font.pixelSize: Math.round(_sp * 0.85); Layout.fillWidth: true
                            wrapMode: Text.Wrap; elide: Text.ElideRight; maximumLineCount: 2
                        }
                        Rectangle {
                            width: Math.round(_sp * 1.4); height: Math.round(_sp * 1.4)
                            radius: Math.round(_sp * 0.2)
                            color: errDismissHover.hovered ? Qt.rgba(_accentRed.r, _accentRed.g, _accentRed.b, 0.3) : "transparent"
                            Text { anchors.centerIn: parent; text: "✕"; color: _accentRed; font.pixelSize: Math.round(_sp * 1.0) }
                            MouseArea {
                                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: _dismissedError = _agentErr
                            }
                            HoverHandler { id: errDismissHover }
                        }
                    }
                    Behavior on height { NumberAnimation { duration: 150 } }
                }

                // ═══ Autonomous Mode Toggle ═══
                Rectangle {
                    Layout.fillWidth: true
                    height: Math.round(_sp * 3.0)
                    radius: Math.round(_sp * 0.35)
                    visible: _agentOK
                    color: _agentAuto ? Qt.rgba(_accentOrange.r, _accentOrange.g, _accentOrange.b, 0.08) : _bgDark
                    border { color: _agentAuto ? _accentOrange : _borderDim; width: 1 }

                    RowLayout {
                        anchors { fill: parent; leftMargin: Math.round(_sp * 0.8); rightMargin: Math.round(_sp * 0.8) }
                        spacing: Math.round(_sp * 0.4)

                        Text {
                            text: _agentAuto ? "◉ Autonomous" : "○ Manual"
                            color: _agentAuto ? _accentOrange : _textDim
                            font.pixelSize: Math.round(_sp * 1.0)
                            font.weight: Font.Medium
                            Layout.fillWidth: true
                            verticalAlignment: Text.AlignVCenter

                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                enabled: _agentOK && !_agentBusy
                                onClicked: {
                                    var ag = _agentMgr
                                    if (!ag) return
                                    ag.setAutonomous(!ag.isAutonomous)
                                }
                            }
                        }

                        // Token badge — display only, does NOT trigger mode toggle
                        Rectangle {
                            Layout.preferredHeight: Math.round(_sp * 1.8)
                            Layout.preferredWidth: tokText.implicitWidth + Math.round(_sp * 0.8)
                            radius: Math.round(_sp * 0.2)
                            color: Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.08)
                            border { color: _borderDim; width: 1 }
                            Text {
                                id: tokText
                                anchors.centerIn: parent
                                text: _agentOK && _agentMgr ? (_agentMgr.totalTokens / 1000).toFixed(1) + "K tokens" : ""
                                color: _accentCyan
                                font.pixelSize: Math.round(_sp * 0.8)
                                font.weight: Font.Medium
                            }
                            // Block clicks from reaching the mode toggle behind
                            MouseArea { anchors.fill: parent; onClicked: {} }
                        }
                    }
                }

                // ═══ Input Row (ChatGPT-style capsule) ═══
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.round(_sp * 3.4)
                    radius: Math.round(_sp * 1.7)
                    readonly property bool _isDark: qgcPal.globalTheme === 1
                    color: _isDark ? "#2a2a2a" : "#ffffff"
                    border.color: _agentAuto
                                 ? (inputField.activeFocus ? _accentOrange : Qt.rgba(_accentOrange.r, _accentOrange.g, _accentOrange.b, 0.4))
                                 : (inputField.activeFocus ? _accentCyan : _borderDim)
                    border.width: 1

                    TextField {
                        id: inputField
                        anchors {
                            left: parent.left; right: sendBtn.left
                            top: parent.top; bottom: parent.bottom
                            leftMargin: Math.round(_sp * 0.8); rightMargin: Math.round(_sp * 0.3)
                        }
                        font.pixelSize: Math.round(_sp * 1.05)
                        placeholderText: !_agentOK ? "Agent not configured..."
                                       : _agentAuto ? "Autonomous mode — commands execute without confirmation"
                                       : "Type a message..."
                        enabled: _agentOK && !_agentBusy
                        verticalAlignment: TextInput.AlignVCenter
                        onAccepted: _doSend()
                        background: Rectangle { color: "transparent" }
                        color: parent._isDark ? "#eeeeee" : "#333333"
                        placeholderTextColor: _agentAuto
                                              ? (parent._isDark ? "#a87a5a" : "#c4885a")
                                              : (parent._isDark ? "#777777" : "#aaaaaa")
                    }

                    // Embedded circular send / stop button
                    Rectangle {
                        id: sendBtn
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter
                                  rightMargin: Math.round(_sp * 0.3) }
                        width: Math.round(_sp * 2.8); height: width; radius: width / 2
                        property bool sendBtnEnabled: _agentOK && (_agentBusy || inputField.text.trim().length > 0)
                        color: _agentBusy ? _accentRed
                             : (sendBtnEnabled ? (sendHover.hovered ? _accentCyan : Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.85))
                                               : Qt.rgba(_borderDim.r, _borderDim.g, _borderDim.b, 0.3))
                        Text {
                            id: sendLbl; anchors.centerIn: parent
                            text: _agentBusy ? "■" : "↑"
                            color: _agentBusy ? qgcPal.buttonHighlightText : (parent.sendBtnEnabled ? qgcPal.primaryButtonText : _textDim)
                            font { pixelSize: Math.round(_sp * 1.2); weight: Font.Bold }
                        }
                        MouseArea {
                            id: sendArea
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            enabled: parent.sendBtnEnabled
                            onClicked: _doSend()
                        }
                        HoverHandler { id: sendHover; enabled: parent.sendBtnEnabled && !_agentBusy }
                    }
                }

                // ═══ Bottom controls (JARVIS-style button row) ═══
                RowLayout {
                    Layout.fillWidth: true; spacing: Math.round(_sp * 0.3)

                    Repeater {
                        model: [
                            { label: _agentAuto ? "◉ AUTO" : "○ AUTO", active: _agentAuto, action: "auto" },
                            { label: "✕ 清空", active: false, action: "clear" },
                            { label: "📋 复制", active: false, action: "copyAll" }
                        ]
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.round(_sp * 2.8)
                            Layout.minimumHeight: Math.round(_sp * 2.8)
                            radius: Math.round(_sp * 0.3)
                            color: modelData.active ? Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.08) : "transparent"
                            border.color: modelData.active ? _accentCyan : _borderDim; border.width: 1
                            Text {
                                anchors.centerIn: parent; text: modelData.label
                                color: modelData.active ? _accentCyan : _textDim
                                font { pixelSize: Math.round(_sp * 0.9); bold: true }
                            }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (modelData.action === "auto") {
                                        var ag = _agentMgr
                                        if (!ag) return
                                        ag.setAutonomous(!ag.isAutonomous)
                                    } else if (modelData.action === "clear") {
                                        if (_agentMgr && typeof _agentMgr.clearConversationHistory === 'function')
                                            _agentMgr.clearConversationHistory()
                                        chatModel.clear()
                                    } else if (modelData.action === "copyAll") {
                                        var allText = ""
                                        for (var i = 0; i < chatModel.count; i++) {
                                            var item = chatModel.get(i)
                                            if (item.role !== "thinking") {
                                                allText += "[" + item.role + "] " + (item.content || "") + "\n\n"
                                            }
                                        }
                                        _copyToClipboard(allText)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ──────────── TAB 1: DIAGNOSTICS ────────────
            Flickable {
                contentWidth: width
                contentHeight: diagCol.implicitHeight
                clip: true; flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: diagCol
                    width: parent.width
                    spacing: Math.round(_sp * 0.6)

                    Text {
                        text: "◉ 飞行器诊断"
                        color: _accentCyan
                        font { pixelSize: Math.round(_sp * 1.1); bold: true }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: _borderDim }

                    // Vehicle info card
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: diagInfoCol.implicitHeight + Math.round(_sp * 1.4)
                        color: _bgDark; radius: Math.round(_sp * 0.4)
                        border.color: _borderDim; border.width: 1
                        ColumnLayout {
                            id: diagInfoCol
                            anchors { fill: parent; margins: Math.round(_sp * 0.7) }
                            spacing: Math.round(_sp * 0.3)
                            Text { text: "连接状态"; color: _textDim; font { pixelSize: Math.round(_sp * 0.75); bold: true } }
                            Text { text: _statusConnected ? "✅ 已连接" : "❌ 未连接"
                                color: _statusConnected ? _accentGreen : _accentRed; font.pixelSize: _sp }
                            Text { text: "飞行模式: " + _statusMode; color: _textPrimary; font.pixelSize: _sp; visible: _statusConnected }
                            Text { text: "电池: " + _statusBat + "%"; color: _textPrimary; font.pixelSize: _sp; visible: _statusConnected }
                            Text { text: "GPS 卫星: " + _statusGps; color: _textPrimary; font.pixelSize: _sp; visible: _statusConnected }
                        }
                    }

                    // Battery gauge
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: Math.round(_sp * 6)
                        color: _bgDark; radius: Math.round(_sp * 0.4)
                        border.color: _borderDim; border.width: 1; visible: _statusConnected
                        ColumnLayout {
                            anchors { fill: parent; margins: Math.round(_sp * 0.7) }
                            spacing: Math.round(_sp * 0.4)
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "电池"; color: _textDim; font { pixelSize: Math.round(_sp * 0.75); bold: true } }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: _statusBat + "%"
                                    color: parseInt(_statusBat) > 20 ? _accentGreen : _accentRed
                                    font { pixelSize: Math.round(_sp * 1.2); bold: true }
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true; height: Math.round(_sp * 0.5)
                                color: Qt.rgba(_borderDim.r, _borderDim.g, _borderDim.b, 0.3); radius: Math.round(_sp * 0.2)
                                Rectangle {
                                    width: parent.width * Math.min(parseInt(_statusBat) / 100, 1)
                                    height: parent.height; radius: parent.radius
                                    color: parseInt(_statusBat) > 50 ? _accentGreen : parseInt(_statusBat) > 20 ? _accentOrange : _accentRed
                                    Behavior on width { NumberAnimation { duration: 300 } }
                                }
                            }
                        }
                    }

                    // Agent status card
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: agentDiagCol.implicitHeight + Math.round(_sp * 1.4)
                        color: _bgDark; radius: Math.round(_sp * 0.4)
                        border.color: _borderDim; border.width: 1
                        ColumnLayout {
                            id: agentDiagCol
                            anchors { fill: parent; margins: Math.round(_sp * 0.7) }
                            spacing: Math.round(_sp * 0.3)
                            Text { text: "AI Agent"; color: _textDim; font { pixelSize: Math.round(_sp * 0.75); bold: true } }
                            Text { text: "Status: " + (_agentBusy ? "Processing" : _agentOK ? "Ready" : "Not Configured")
                                color: _agentBusy ? _accentOrange : _agentOK ? _accentGreen : _accentRed
                                font.pixelSize: _sp }
                            Text { text: "Mode: " + (_agentAuto ? "Autonomous" : "Manual"); color: _textPrimary; font.pixelSize: _sp; visible: _agentOK }
                            Text { text: "Tokens: " + (_agentOK && _agentMgr ? (_agentMgr.totalTokens / 1000).toFixed(1) + "K" : "0"); color: _textPrimary; font.pixelSize: _sp; visible: _agentOK }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // ──────────── TAB 2: CHARTS ────────────
            Loader {
                id: chartLoader
                active: currentTab === 2 && _agentOK && _agentMgr !== null
                source: "qrc:/Custom/qml/QGroundControl/FlyView/LogChartView.qml"
                onLoaded: {
                    if (item) item.logModel = _agentMgr ? _agentMgr.logAnalysisModel : null
                }
                onActiveChanged: {
                    if (active && item && _agentMgr) {
                        item.logModel = _agentMgr.logAnalysisModel
                    }
                }
            }

            // ──────────── TAB 3: SETTINGS ────────────
            Flickable {
                contentWidth: width
                contentHeight: settingsCol.implicitHeight
                clip: true; flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: settingsCol
                    width: parent.width
                    spacing: Math.round(_sp * 0.6)

                    // ── Header + connection status ──
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "⚙ API 设置"
                            color: _accentCyan
                            font { pixelSize: Math.round(_sp * 1.1); bold: true }
                        }
                        Item { Layout.fillWidth: true }
                        // Connection status indicator
                        Rectangle {
                            Layout.preferredWidth: statusRow.implicitWidth + _sp * 1.0
                            Layout.preferredHeight: Math.round(_sp * 1.8)
                            Layout.alignment: Qt.AlignVCenter
                            radius: height / 2
                            color: Qt.rgba(_connColor.r, _connColor.g, _connColor.b, 0.12)
                            border.color: _connColor; border.width: 1
                            readonly property color _connColor: {
                                if (!_agentOK) return _textDim
                                var s = _agentMgr.connectionStatus
                                if (s === 1) return _accentOrange      // Connecting
                                if (s === 2) return _accentGreen        // Connected
                                if (s === 3) return _accentRed          // Failed
                                return _textDim                          // Unknown
                            }
                            RowLayout {
                                id: statusRow
                                anchors.centerIn: parent
                                spacing: Math.round(_sp * 0.25)
                                Rectangle {
                                    width: Math.round(_sp * 0.5); height: width
                                    radius: width / 2; color: parent.parent._connColor
                                    SequentialAnimation on opacity {
                                        running: _agentOK && _agentMgr.connectionStatus === 1
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.3; duration: 600 }
                                        NumberAnimation { to: 1.0; duration: 600 }
                                    }
                                }
                                Text {
                                    text: {
                                        if (!_agentOK) return "未就绪"
                                        var s = _agentMgr.connectionStatus
                                        if (s === 1) return "连接中..."
                                        if (s === 2) return "已连接"
                                        if (s === 3) return "连接失败"
                                        return "未测试"
                                    }
                                    color: parent.parent._connColor
                                    font.pixelSize: Math.round(_sp * 0.8)
                                    font.bold: true
                                }
                            }
                        }
                    }

                    // ── Card: 供应商 ──
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: providerInner.implicitHeight + Math.round(_sp * 1.2)
                        radius: Math.round(_sp * 0.4)
                        color: _bgCard
                        border.color: _borderDim; border.width: 1
                        ColumnLayout {
                            id: providerInner
                            anchors.fill: parent; anchors.margins: Math.round(_sp * 0.6)
                            spacing: Math.round(_sp * 0.4)
                            Text {
                                text: "供应商"
                                color: _textDim
                                font.pixelSize: Math.round(_sp * 0.75)
                                font.capitalization: Font.AllUppercase
                            }
                            ComboBox {
                                id: providerCombo
                                Layout.fillWidth: true
                                model: ["DeepSeek", "DashScope (通义千问)", "智谱GLM", "OpenAI", "Ollama (本地)", "自定义"]
                                currentIndex: _detectProviderIndex()
                                onActivated: _applyProviderPreset(currentIndex)
                                background: Rectangle {
                                    radius: Math.round(_sp * 0.3); color: _bgDark
                                    border { color: _borderDim; width: 1 }
                                }
                                contentItem: Text {
                                    text: parent.displayText; color: qgcPal.buttonText
                                    font.pixelSize: Math.round(_sp * 1.1)
                                    verticalAlignment: Text.AlignVCenter; leftPadding: Math.round(_sp * 0.6)
                                }
                            }
                        }
                    }

                    // ── Card: 认证配置 ──
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: authInner.implicitHeight + Math.round(_sp * 1.2)
                        radius: Math.round(_sp * 0.4)
                        color: _bgCard
                        border.color: _borderDim; border.width: 1
                        ColumnLayout {
                            id: authInner
                            anchors.fill: parent; anchors.margins: Math.round(_sp * 0.6)
                            spacing: Math.round(_sp * 0.4)

                            Text {
                                text: "认证配置"
                                color: _textDim
                                font.pixelSize: Math.round(_sp * 0.75)
                                font.capitalization: Font.AllUppercase
                            }

                            // API Key
                            Text { text: "API Key（必填）"; font.pixelSize: Math.round(_sp * 0.9); color: _textPrimary }
                            RowLayout {
                                Layout.fillWidth: true; spacing: Math.round(_sp * 0.3)
                                TextField {
                                    id: apiKeyField
                                    Layout.fillWidth: true
                                    font.pixelSize: Math.round(_sp * 1.0)
                                    echoMode: TextInput.Password
                                    placeholderText: "sk-xxxxxxxxxxxxxxxx"
                                    selectByMouse: true
                                    Component.onCompleted: { if (_agentOK) text = _agentMgr.apiKey }
                                    onEditingFinished: { if (_agentMgr) _agentMgr.setApiKey(text) }
                                    background: Rectangle {
                                        radius: Math.round(_sp * 0.3); color: _bgDark
                                        border { color: apiKeyField.text.length === 0 ? _accentRed : _borderDim; width: 1 }
                                    }
                                    color: _textPrimary
                                    placeholderTextColor: _textDim
                                }
                                Rectangle {
                                    Layout.preferredWidth: toggleApiText.implicitWidth + _sp * 1.4
                                    Layout.preferredHeight: Math.round(_sp * 2.0)
                                    Layout.alignment: Qt.AlignVCenter
                                    radius: Math.round(_sp * 0.25)
                                    color: apiTogHover.hovered ? Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.1) : "transparent"
                                    Text {
                                        id: toggleApiText; anchors.centerIn: parent
                                        text: apiKeyField.echoMode === TextInput.Normal ? "隐藏" : "显示"
                                        color: _accentCyan; font.pixelSize: _sp
                                    }
                                    MouseArea {
                                        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: { apiKeyField.echoMode = apiKeyField.echoMode === TextInput.Normal ? TextInput.Password : TextInput.Normal }
                                    }
                                    HoverHandler { id: apiTogHover }
                                }
                            }

                            // Base URL + Test button
                            Text { text: "Base URL"; font.pixelSize: Math.round(_sp * 0.9); color: _textPrimary }
                            RowLayout {
                                Layout.fillWidth: true; spacing: Math.round(_sp * 0.3)
                                TextField {
                                    id: baseUrlField
                                    Layout.fillWidth: true
                                    font.pixelSize: Math.round(_sp * 0.95)
                                    placeholderText: "https://api.deepseek.com/v1"
                                    selectByMouse: true
                                    Component.onCompleted: { if (_agentOK) text = _agentMgr.baseApiEndpoint() }
                                    onEditingFinished: { if (_agentMgr) _agentMgr.setApiEndpoint(text) }
                                    background: Rectangle {
                                        radius: Math.round(_sp * 0.3); color: _bgDark
                                        border { color: baseUrlField.text.length === 0 ? _accentRed : _borderDim; width: 1 }
                                    }
                                    color: _textPrimary
                                    placeholderTextColor: _textDim
                                }
                                Rectangle {
                                    Layout.preferredWidth: testBtn.implicitWidth + _sp * 1.2
                                    Layout.preferredHeight: Math.round(_sp * 2.0)
                                    Layout.alignment: Qt.AlignVCenter
                                    radius: Math.round(_sp * 0.3)
                                    color: testEnabled ? (testHover.hovered ? _accentCyan : Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.8))
                                                       : _bgDark
                                    border { color: testEnabled ? _accentCyan : _borderDim; width: 1 }
                                    property bool testEnabled: _agentOK && baseUrlField.text.length > 0 && apiKeyField.text.length > 0 && !_agentBusy
                                    Text {
                                        id: testBtn; anchors.centerIn: parent
                                        text: _agentOK && _agentMgr.connectionStatus === 1 ? "..." : "测试连接"
                                        color: parent.testEnabled ? qgcPal.primaryButtonText : _textDim
                                        font.pixelSize: _sp
                                    }
                                    MouseArea {
                                        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        enabled: parent.testEnabled
                                        onClicked: {
                                            if (_agentMgr) {
                                                _agentMgr.setApiKey(apiKeyField.text)
                                                _agentMgr.setApiEndpoint(baseUrlField.text)
                                                _agentMgr.testConnection()
                                            }
                                        }
                                    }
                                    HoverHandler { id: testHover; enabled: parent.testEnabled }
                                }
                            }
                            Text {
                                Layout.fillWidth: true; font.pixelSize: Math.round(_sp * 0.7); color: _textDim
                                text: "只需输入到 /v1 或域名，自动拼接 /chat/completions"
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    // ── Card: 模型 ──
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: modelInner.implicitHeight + Math.round(_sp * 1.2)
                        radius: Math.round(_sp * 0.4)
                        color: _bgCard
                        border.color: _borderDim; border.width: 1
                        ColumnLayout {
                            id: modelInner
                            anchors.fill: parent; anchors.margins: Math.round(_sp * 0.6)
                            spacing: Math.round(_sp * 0.4)

                            Text {
                                text: "模型"
                                color: _textDim
                                font.pixelSize: Math.round(_sp * 0.75)
                                font.capitalization: Font.AllUppercase
                            }
                            ComboBox {
                                id: modelCombo
                                Layout.fillWidth: true
                                editable: true
                                model: _fetchedModels
                                onModelChanged: {
                                    if (_agentMgr && _fetchedModels.length > 0) {
                                        var m = _agentMgr.modelName
                                        for (var i = 0; i < _fetchedModels.length; i++) {
                                            if (_fetchedModels[i] === m) { currentIndex = i; return }
                                        }
                                        currentIndex = -1; editText = m
                                    }
                                }
                                Component.onCompleted: { if (_agentMgr) editText = _agentMgr.modelName }
                                onActivated: { if (_agentMgr && currentText.length > 0) _agentMgr.setModelName(currentText) }
                                onAccepted: { if (_agentMgr && editText.length > 0) _agentMgr.setModelName(editText) }
                                background: Rectangle {
                                    radius: Math.round(_sp * 0.3); color: _bgDark
                                    border { color: _borderDim; width: 1 }
                                }
                                contentItem: Text {
                                    text: parent.displayText; color: qgcPal.buttonText
                                    font.pixelSize: Math.round(_sp * 1.1)
                                    verticalAlignment: Text.AlignVCenter; leftPadding: Math.round(_sp * 0.6)
                                }
                            }
                            Text {
                                font.pixelSize: Math.round(_sp * 0.7); color: _textDim
                                text: _fetchedModels.length > 0
                                    ? "已获取 " + _fetchedModels.length + " 个模型，也可手动输入"
                                    : "点击「测试连接」获取可用模型，或直接输入模型名"
                            }
                        }
                    }

                    // ── Card: 其他设置 ──
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: otherInner.implicitHeight + Math.round(_sp * 1.2)
                        radius: Math.round(_sp * 0.4)
                        color: _bgCard
                        border.color: _borderDim; border.width: 1
                        ColumnLayout {
                            id: otherInner
                            anchors.fill: parent; anchors.margins: Math.round(_sp * 0.6)
                            spacing: Math.round(_sp * 0.4)

                            Text {
                                text: "其他设置"
                                color: _textDim
                                font.pixelSize: Math.round(_sp * 0.75)
                                font.capitalization: Font.AllUppercase
                            }
                            Text { text: "高德地图 API Key（逆地理编码）"; font.pixelSize: Math.round(_sp * 0.9); color: _textPrimary }
                            TextField {
                                id: geocodeKeyField
                                Layout.fillWidth: true
                                font.pixelSize: Math.round(_sp * 1.0)
                                echoMode: TextInput.Password
                                placeholderText: "高德 Web 服务 API Key"
                                selectByMouse: true
                                Component.onCompleted: { if (_agentOK) text = _agentMgr.geocodeApiKey }
                                onEditingFinished: { if (_agentMgr) _agentMgr.setGeocodeApiKey(text) }
                                background: Rectangle {
                                    radius: Math.round(_sp * 0.3); color: _bgDark
                                    border { color: _borderDim; width: 1 }
                                }
                                color: _textPrimary
                                placeholderTextColor: _textDim
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    // ── Save & apply button ──
                    Rectangle {
                        Layout.fillWidth: true
                        height: Math.round(_sp * 2.6)
                        radius: Math.round(_sp * 0.3)
                        color: saveHover.hovered ? _accentCyan : Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.85)
                        Text {
                            anchors.centerIn: parent; text: "保存并返回"
                            color: qgcPal.primaryButtonText; font.pixelSize: Math.round(_sp * 1.1)
                            font.weight: Font.Bold
                        }
                        MouseArea {
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (_agentMgr) {
                                    _agentMgr.setApiKey(apiKeyField.text)
                                    _agentMgr.setApiEndpoint(baseUrlField.text)
                                    _agentMgr.setModelName(modelCombo.editText)
                                    _agentMgr.setGeocodeApiKey(geocodeKeyField.text)
                                }
                                currentTab = 0
                            }
                        }
                        HoverHandler { id: saveHover }
                    }

                    Text {
                        Layout.fillWidth: true
                        font.pixelSize: Math.round(_sp * 0.75)
                        color: _accentGreen
                        wrapMode: Text.Wrap
                        text: "配置已加密存储于本地\n切换供应商会自动填充 URL 和默认模型"
                    }
                }
            }

            // ──────────── TAB 4: KNOWLEDGE BASE (RAG) ────────────
            Flickable {
                contentWidth: width
                contentHeight: ragCol.implicitHeight
                clip: true; flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: ragCol
                    width: parent.width
                    spacing: Math.round(_sp * 0.6)

                    // ── Header + RAG status ──
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "📚 知识库检索 (RAG)"
                            color: _accentCyan
                            font { pixelSize: Math.round(_sp * 1.1); bold: true }
                        }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            Layout.preferredWidth: ragStatusText.implicitWidth + _sp * 1.0
                            Layout.preferredHeight: Math.round(_sp * 1.8)
                            Layout.alignment: Qt.AlignVCenter
                            radius: height / 2
                            readonly property bool ragOn: _agentMgr ? _agentMgr.ragEnabled : false
                            color: ragOn ? Qt.rgba(_accentGreen.r, _accentGreen.g, _accentGreen.b, 0.12)
                                         : Qt.rgba(_textDim.r, _textDim.g, _textDim.b, 0.08)
                            border.color: ragOn ? _accentGreen : _borderDim; border.width: 1
                            Text {
                                id: ragStatusText
                                anchors.centerIn: parent
                                text: parent.ragOn ? "● 已启用" : "○ 未启用"
                                color: parent.ragOn ? _accentGreen : _textDim
                                font { pixelSize: Math.round(_sp * 0.8); bold: true }
                            }
                        }
                    }

                    // ── Description ──
                    Text {
                        Layout.fillWidth: true
                        font.pixelSize: Math.round(_sp * 0.75)
                        color: _textDim
                        wrapMode: Text.Wrap
                        text: "配置云端知识库检索端点。每次对话自动检索相关知识，LLM 也可主动调用 retrieve_knowledge 工具深度检索。"
                    }

                    // ── Card: 启用设置 ──
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: ragToggleInner.implicitHeight + Math.round(_sp * 1.2)
                        radius: Math.round(_sp * 0.4)
                        color: _bgCard
                        border.color: _borderDim; border.width: 1
                        ColumnLayout {
                            id: ragToggleInner
                            anchors.fill: parent; anchors.margins: Math.round(_sp * 0.6)
                            spacing: Math.round(_sp * 0.4)

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: "启用 RAG 检索"
                                    color: _textPrimary
                                    font { pixelSize: Math.round(_sp * 0.95); bold: true }
                                    Layout.fillWidth: true
                                }
                                // Custom toggle
                                Rectangle {
                                    Layout.preferredWidth: Math.round(_sp * 3.6)
                                    Layout.preferredHeight: Math.round(_sp * 1.8)
                                    Layout.alignment: Qt.AlignVCenter
                                    radius: height / 2
                                    readonly property bool ragOn: _agentMgr ? _agentMgr.ragEnabled : false
                                    color: ragOn ? _accentGreen : _bgDark
                                    border.color: ragOn ? _accentGreen : _borderDim
                                    border.width: 1
                                    Behavior on color { ColorAnimation { duration: 150 } }

                                    Rectangle {
                                        readonly property real _knob: parent.height - Math.round(_sp * 0.5)
                                        width: _knob; height: _knob
                                        radius: width / 2
                                        anchors.verticalCenter: parent.verticalCenter
                                        x: parent.ragOn ? parent.width - width - Math.round(_sp * 0.25) : Math.round(_sp * 0.25)
                                        color: parent.ragOn ? "#ffffff" : _textDim
                                        Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                                    }

                                    MouseArea {
                                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (_agentMgr) _agentMgr.setRagEnabled(!parent.ragOn)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Card: RAG 配置 ──
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: ragConfigInner.implicitHeight + Math.round(_sp * 1.2)
                        radius: Math.round(_sp * 0.4)
                        color: _bgCard
                        border.color: _borderDim; border.width: 1
                        ColumnLayout {
                            id: ragConfigInner
                            anchors.fill: parent; anchors.margins: Math.round(_sp * 0.6)
                            spacing: Math.round(_sp * 0.4)

                            Text {
                                text: "RAG 配置"
                                color: _textDim
                                font.pixelSize: Math.round(_sp * 0.75)
                                font.capitalization: Font.AllUppercase
                            }

                            // Endpoint URL
                            Text { text: "检索端点 URL"; font.pixelSize: Math.round(_sp * 0.9); color: _textPrimary }
                            TextField {
                                id: ragEndpointField
                                Layout.fillWidth: true
                                text: _agentMgr ? _agentMgr.ragEndpoint : ""
                                placeholderText: "https://dify.example.com/v1/datasets/{id}/retrieve"
                                color: _textPrimary
                                placeholderTextColor: _textDim
                                font.pixelSize: Math.round(_sp * 0.95)
                                selectByMouse: true
                                background: Rectangle {
                                    radius: Math.round(_sp * 0.3); color: _bgDark
                                    border { color: ragEndpointHover.hovered ? _accentCyan : _borderDim; width: 1 }
                                }
                                onEditingFinished: {
                                    if (_agentMgr) _agentMgr.setRagEndpoint(text)
                                }
                                HoverHandler { id: ragEndpointHover }
                            }
                            Text {
                                font.pixelSize: Math.round(_sp * 0.7); color: _textDim
                                text: "Dify: https://{host}/v1/datasets/{dataset_id}/retrieve"
                            }

                            // API Key
                            Text { text: "RAG API Key（留空则复用主 API Key）"; font.pixelSize: Math.round(_sp * 0.9); color: _textPrimary }
                            RowLayout {
                                Layout.fillWidth: true; spacing: Math.round(_sp * 0.3)
                                TextField {
                                    id: ragKeyField
                                    Layout.fillWidth: true
                                    text: _agentMgr ? _agentMgr.ragApiKey : ""
                                    placeholderText: "留空复用主 API Key"
                                    echoMode: TextInput.Password
                                    color: qgcPal.textFieldText
                                    font.pixelSize: Math.round(_sp * 0.95)
                                    selectByMouse: true
                                    background: Rectangle {
                                        radius: Math.round(_sp * 0.3); color: _bgDark
                                        border { color: ragKeyHover.hovered ? _accentCyan : _borderDim; width: 1 }
                                    }
                                    onEditingFinished: {
                                        if (_agentMgr) _agentMgr.setRagApiKey(text)
                                    }
                                    HoverHandler { id: ragKeyHover }
                                }
                                Rectangle {
                                    Layout.preferredWidth: ragKeyToggleText.implicitWidth + _sp * 1.4
                                    Layout.preferredHeight: Math.round(_sp * 2.0)
                                    Layout.alignment: Qt.AlignVCenter
                                    radius: Math.round(_sp * 0.25)
                                    color: ragKeyTogHover.hovered ? Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.1) : "transparent"
                                    Text {
                                        id: ragKeyToggleText; anchors.centerIn: parent
                                        text: ragKeyField.echoMode === TextInput.Normal ? "隐藏" : "显示"
                                        color: _accentCyan; font.pixelSize: _sp
                                    }
                                    MouseArea {
                                        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: { ragKeyField.echoMode = ragKeyField.echoMode === TextInput.Normal ? TextInput.Password : TextInput.Normal }
                                    }
                                    HoverHandler { id: ragKeyTogHover }
                                }
                            }

                            // Test button + result
                            RowLayout {
                                Layout.fillWidth: true; spacing: Math.round(_sp * 0.3)
                                Rectangle {
                                    Layout.preferredWidth: ragTestBtn.implicitWidth + _sp * 1.2
                                    Layout.preferredHeight: Math.round(_sp * 2.0)
                                    Layout.alignment: Qt.AlignVCenter
                                    radius: Math.round(_sp * 0.3)
                                    property bool testEnabled: _agentMgr && _agentMgr.ragEnabled && ragEndpointField.text.length > 0 && !_agentBusy
                                    color: testEnabled ? (testRagHover.hovered ? _accentCyan : Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.8))
                                                       : _bgDark
                                    border { color: testEnabled ? _accentCyan : _borderDim; width: 1 }
                                    Text {
                                        id: ragTestBtn; anchors.centerIn: parent
                                        text: "🔌 测试连接"
                                        color: parent.testEnabled ? qgcPal.primaryButtonText : _textDim
                                        font { pixelSize: _sp; bold: true }
                                    }
                                    MouseArea {
                                        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        enabled: parent.testEnabled
                                        onClicked: {
                                            if (_agentMgr) {
                                                _agentMgr.setRagEndpoint(ragEndpointField.text)
                                                _agentMgr.setRagApiKey(ragKeyField.text)
                                                _agentMgr.testRagConnection()
                                            }
                                        }
                                    }
                                    HoverHandler { id: testRagHover }
                                }
                                Text {
                                    id: ragTestResultText
                                    Layout.fillWidth: true
                                    font.pixelSize: Math.round(_sp * 0.8)
                                    color: _textDim
                                    wrapMode: Text.Wrap
                                    visible: text.length > 0
                                }
                            }
                        }
                    }

                    // ── Card: 使用说明 ──
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: ragInfoCol.implicitHeight + Math.round(_sp * 1.2)
                        radius: Math.round(_sp * 0.4)
                        color: Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.05)
                        border { color: Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.2); width: 1 }
                        ColumnLayout {
                            id: ragInfoCol
                            anchors.fill: parent; anchors.margins: Math.round(_sp * 0.6)
                            spacing: Math.round(_sp * 0.3)

                            Text {
                                Layout.fillWidth: true
                                text: "📖 使用说明"
                                color: _accentCyan
                                font { pixelSize: Math.round(_sp * 0.9); bold: true }
                            }
                            Text {
                                Layout.fillWidth: true
                                font.pixelSize: Math.round(_sp * 0.75)
                                color: _textPrimary
                                wrapMode: Text.Wrap
                                text: "1. 在 Dify/百炼平台创建知识库，上传无人机文档\n" +
                                      "2. 获取知识库检索 API 端点和 Key\n" +
                                      "3. 填入上方配置并启用\n" +
                                      "4. 对话时自动检索 top-3 知识片段注入上下文\n" +
                                      "5. LLM 也可通过 retrieve_knowledge 工具主动检索"
                            }
                            Text {
                                Layout.fillWidth: true
                                font.pixelSize: Math.round(_sp * 0.7)
                                color: _textDim
                                wrapMode: Text.Wrap
                                text: "推荐平台：Dify（自建/云）、阿里云百炼、MaxKB\n兼容 Dify retrieve 端点格式"
                            }
                        }
                    }
                }
            }

            // ──────────── TAB 5: ARCHIVES ────────────
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                spacing: Math.round(_sp * 0.4)

                // Properties for archive view state
                property string selectedArchiveId: ""
                property var selectedMessages: []

                // ── Header ──
                RowLayout {
                    Layout.fillWidth: true; spacing: Math.round(_sp * 0.4)

                    Text {
                        text: "Chat Archives"
                        color: _textPrimary; font { pixelSize: _sp; bold: true }
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: _agentOK ? (_agentMgr.archiveCount() + " archived") : "0 archived"
                        color: _textDim; font.pixelSize: Math.round(_sp * 0.8)
                    }

                    // Archive Current button
                    Rectangle {
                        Layout.preferredWidth: archiveBtn.implicitWidth + _sp * 1.2
                        Layout.preferredHeight: Math.round(_sp * 2.0)
                        radius: Math.round(_sp * 0.3)
                        color: archiveBtnHover.hovered ? Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.2) : _bgDark
                        border { color: _accentCyan; width: 1 }
                        Text {
                            id: archiveBtn; anchors.centerIn: parent
                            text: "📦 Archive Current"; color: _accentCyan
                            font { pixelSize: Math.round(_sp * 0.85); bold: true }
                        }
                        MouseArea {
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (_agentOK) {
                                    _agentMgr.archiveCurrentChat()
                                    archiveList.model = _agentMgr.listArchives()
                                }
                            }
                        }
                        HoverHandler { id: archiveBtnHover }
                    }

                    // Refresh button
                    Rectangle {
                        Layout.preferredWidth: Math.round(_sp * 2.4)
                        Layout.preferredHeight: Math.round(_sp * 2.0)
                        radius: Math.round(_sp * 0.3)
                        color: refreshArchHover.hovered ? Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.15) : _bgDark
                        border { color: _borderDim; width: 1 }
                        Text {
                            anchors.centerIn: parent; text: "↻"
                            color: _textPrimary; font.pixelSize: _sp
                        }
                        MouseArea {
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (_agentOK) archiveList.model = _agentMgr.listArchives()
                            }
                        }
                        HoverHandler { id: refreshArchHover }
                    }
                }

                // ── Content area ──
                // List view (when no archive selected) or detail view (when selected)
                StackLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    currentIndex: parent.selectedArchiveId === "" ? 0 : 1

                    // ── Archive List ──
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true

                        ListView {
                            id: archiveList
                            model: _agentOK ? _agentMgr.listArchives() : []
                            spacing: Math.round(_sp * 0.3)
                            delegate: Rectangle {
                                width: archiveList.width
                                height: archItemCol.implicitHeight + Math.round(_sp * 0.8)
                                radius: Math.round(_sp * 0.3)
                                color: archItemHover.hovered ? Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.08) : _bgDark
                                border { color: _borderDim; width: 1 }

                                ColumnLayout {
                                    id: archItemCol
                                    anchors { fill: parent; margins: Math.round(_sp * 0.5) }
                                    spacing: Math.round(_sp * 0.2)

                                    RowLayout {
                                        Layout.fillWidth: true; spacing: Math.round(_sp * 0.3)

                                        Text {
                                            text: modelData.timestamp
                                            color: _accentCyan; font { pixelSize: Math.round(_sp * 0.85); bold: true }
                                        }
                                        Item { Layout.fillWidth: true }
                                        Text {
                                            text: modelData.messageCount + " msgs"
                                            color: _textDim; font.pixelSize: Math.round(_sp * 0.75)
                                        }

                                        // Delete button
                                        Rectangle {
                                            Layout.preferredWidth: Math.round(_sp * 1.8)
                                            Layout.preferredHeight: Math.round(_sp * 1.6)
                                            radius: Math.round(_sp * 0.2)
                                            color: delArchHover.hovered ? Qt.rgba(_accentRed.r, _accentRed.g, _accentRed.b, 0.2) : "transparent"
                                            border { color: _accentRed; width: 1 }
                                            Text {
                                                anchors.centerIn: parent; text: "✕"
                                                color: _accentRed; font.pixelSize: Math.round(_sp * 0.7)
                                            }
                                            MouseArea {
                                                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    if (_agentOK) {
                                                        _agentMgr.deleteArchive(modelData.id)
                                                        archiveList.model = _agentMgr.listArchives()
                                                    }
                                                }
                                            }
                                            HoverHandler { id: delArchHover }
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.preview || "(empty)"
                                        color: _textDim; font.pixelSize: Math.round(_sp * 0.75)
                                        elide: Text.ElideRight; maximumLineCount: 2
                                        wrapMode: Text.Wrap
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (_agentOK) {
                                            var msgs = _agentMgr.loadArchiveMessages(modelData.id)
                                            parent.parent.parent.parent.selectedArchiveId = modelData.id
                                            parent.parent.parent.parent.selectedMessages = msgs
                                        }
                                    }
                                }
                                HoverHandler { id: archItemHover }
                            }

                            // Empty state
                            Text {
                                anchors.centerIn: parent
                                visible: archiveList.count === 0
                                text: "No archived chats.\nClick 'Archive Current' to save."
                                color: _textDim; font.pixelSize: _sp
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    // ── Archive Detail View ──
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        spacing: Math.round(_sp * 0.4)

                        // Back button + title
                        RowLayout {
                            Layout.fillWidth: true; spacing: Math.round(_sp * 0.4)

                            Rectangle {
                                Layout.preferredWidth: backBtn.implicitWidth + _sp * 1.0
                                Layout.preferredHeight: Math.round(_sp * 2.0)
                                radius: Math.round(_sp * 0.3)
                                color: backHover.hovered ? Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.15) : _bgDark
                                border { color: _borderDim; width: 1 }
                                Text {
                                    id: backBtn; anchors.centerIn: parent
                                    text: "← Back"; color: _accentCyan
                                    font { pixelSize: Math.round(_sp * 0.85); bold: true }
                                }
                                MouseArea {
                                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        var tab = parent.parent.parent
                                        tab.selectedArchiveId = ""
                                        tab.selectedMessages = []
                                    }
                                }
                                HoverHandler { id: backHover }
                            }

                            Text {
                                text: "Viewing Archive"
                                color: _textPrimary; font { pixelSize: _sp; bold: true }
                            }
                            Item { Layout.fillWidth: true }
                        }

                        // Message list
                        ScrollView {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            clip: true

                            ListView {
                                id: archiveMsgList
                                model: parent.parent.parent.selectedMessages
                                spacing: Math.round(_sp * 0.3)

                                delegate: Rectangle {
                                    width: archiveMsgList.width
                                    height: archMsgCol.implicitHeight + Math.round(_sp * 0.6)
                                    radius: Math.round(_sp * 0.3)
                                    color: modelData.role === "user" ? Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.06) :
                                           modelData.role === "assistant" ? Qt.rgba(_accentGreen.r, _accentGreen.g, _accentGreen.b, 0.06) :
                                           _bgDark
                                    border { color: _borderDim; width: 1 }

                                    ColumnLayout {
                                        id: archMsgCol
                                        anchors { fill: parent; margins: Math.round(_sp * 0.4) }
                                        spacing: Math.round(_sp * 0.15)

                                        Text {
                                            text: modelData.role === "user" ? "User" :
                                                  modelData.role === "assistant" ? "Assistant" :
                                                  modelData.role === "system" ? "System" : modelData.role
                                            color: modelData.role === "user" ? _accentCyan :
                                                   modelData.role === "assistant" ? _accentGreen : _textDim
                                            font { pixelSize: Math.round(_sp * 0.7); bold: true }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.content || ""
                                            color: _textPrimary
                                            font.pixelSize: Math.round(_sp * 0.85)
                                            wrapMode: Text.Wrap
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // ════════════════════════════════════════════════════
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.6)
        visible: _showConfirm
        z: QGroundControl.zOrderTopMost
        opacity: _showConfirm ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 180 } }

        MouseArea { anchors.fill: parent } // block clicks through shade

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - _sp * 3, _sp * 32)
            height: cmdCol.implicitHeight + Math.round(_sp * 3)
            radius: Math.round(_sp * 0.7)
            color: _bgCard
            border { color: _accentOrange; width: 1.5 }
            scale: _showConfirm ? 1 : 0.9
            Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }

            Column {
                id: cmdCol
                anchors { fill: parent; margins: Math.round(_sp * 1.4) }
                spacing: Math.round(_sp * 0.7)

                // Header
                RowLayout {
                    width: parent.width; spacing: Math.round(_sp * 0.5)
                    Rectangle {
                        width: Math.round(_sp * 1.8); height: width; radius: width / 2
                        color: _accentOrange
                        Text {
                            anchors.centerIn: parent; text: "!"; font.pixelSize: Math.round(_sp * 1.1)
                            color: qgcPal.primaryButtonText; font.weight: Font.Bold
                        }
                    }
                    Text {
                        text: _pendingCmd && _pendingCmd.toolName ? _pendingCmd.toolName : "指令"
                        color: _accentOrange; font.pixelSize: Math.round(_sp * 1.3); font.weight: Font.Bold
                    }
                }

                Rectangle { width: parent.width; height: 1; color: _borderDim }

                Text {
                    width: parent.width; text: _pendingDesc
                    color: _textPrimary; font.pixelSize: Math.round(_sp * 1.1); wrapMode: Text.Wrap
                }

                // Parameters list
                Rectangle {
                    width: parent.width
                    height: paramFlow.implicitHeight + Math.round(_sp * 1.0)
                    visible: _pendingCmd && _pendingCmd.parameters && Object.keys(_pendingCmd.parameters).length > 0
                    radius: Math.round(_sp * 0.3)
                    color: _bgDark; border { color: _borderDim; width: 1 }

                    Flow {
                        id: paramFlow
                        anchors { fill: parent; margins: Math.round(_sp * 0.5) }
                        spacing: Math.round(_sp * 0.25)

                        Repeater {
                            model: _pendingCmd && _pendingCmd.parameters ? Object.keys(_pendingCmd.parameters) : []
                            delegate: Rectangle {
                                width: paramLabel.implicitWidth + Math.round(_sp * 1.2)
                                height: paramLabel.implicitHeight + Math.round(_sp * 0.6)
                                radius: Math.round(_sp * 0.2)
                                color: _bgCard
                                Text {
                                    id: paramLabel; anchors.centerIn: parent
                                    text: modelData + ": " + (_pendingCmd.parameters[modelData] || "")
                                    color: _textPrimary; font.pixelSize: Math.round(_sp * 0.85)
                                }
                            }
                        }
                    }
                }

                Text {
                    width: parent.width; text: "请确认是否执行此操作"
                    color: _textPrimary; font.pixelSize: _sp; horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    width: parent.width
                    text: "⏰ " + _confirmSecondsLeft + "s 后自动取消"
                    color: _confirmSecondsLeft <= 5 ? _accentRed : _textDim
                    font.pixelSize: _sp; horizontalAlignment: Text.AlignHCenter
                }

                RowLayout {
                    width: parent.width; spacing: Math.round(_sp * 0.7)
                    Rectangle {
                        Layout.fillWidth: true; height: Math.round(_sp * 2.6)
                        radius: Math.round(_sp * 0.3)
                        color: cancelHover.hovered ? Qt.rgba(qgcPal.buttonHighlight.r, qgcPal.buttonHighlight.g, qgcPal.buttonHighlight.b, 0.2) : _bgDark
                        border { color: _borderDim; width: 1 }
                        Text {
                            anchors.centerIn: parent; text: "✗ 取消"
                            color: _textPrimary; font.pixelSize: Math.round(_sp * 1.1)
                        }
                        MouseArea {
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                _showConfirm = false; _pendingCmd = null
                                if (_agentMgr) _agentMgr.rejectCommand()
                                else chatModel.append({"role": "system", "content": "✗ 指令已取消", "ts": ""})
                            }
                        }
                        HoverHandler { id: cancelHover }
                    }
                    Rectangle {
                        Layout.fillWidth: true; height: Math.round(_sp * 2.6)
                        radius: Math.round(_sp * 0.3)
                        color: confirmHover.hovered ? Qt.rgba(_accentGreen.r, _accentGreen.g, _accentGreen.b, 0.7)
                                                     : Qt.rgba(_accentGreen.r, _accentGreen.g, _accentGreen.b, 0.4)
                        Text {
                            anchors.centerIn: parent; text: "✓ 确认执行"
                            color: _accentGreen; font.pixelSize: Math.round(_sp * 1.1)
                            font.weight: Font.Bold
                        }
                        MouseArea {
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                _showConfirm = false
                                if (_agentMgr) _agentMgr.confirmCommand(_pendingCmd)
                                _pendingCmd = null
                            }
                        }
                        HoverHandler { id: confirmHover }
                    }
                }
            }
        }
    }

    // ════════════════════════════════════════════════════
    //  Onboard Log Picker Overlay
    // ════════════════════════════════════════════════════
    Rectangle {
        id: logPickerOverlay
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.55)
        visible: _showLogPicker
        z: 100

        // Dim background — no MouseArea here so clicks pass through to the card.
        // Close via the ✕ button only.

        Rectangle {
            id: logPickerCard
            width: Math.min(parent.width * 0.88, Math.round(_sp * 32))
            height: Math.min(parent.height * 0.75, Math.round(_sp * 34))
            anchors.centerIn: parent
            color: qgcPal.window
            radius: Math.round(_sp * 0.5)
            border.color: _accentCyan; border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Math.round(_sp * 0.6)
                spacing: Math.round(_sp * 0.4)

                // ── Header ──
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "📡 机载日志"
                        color: qgcPal.buttonHighlightText
                        font.pixelSize: Math.round(_sp * 1.1)
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        width: Math.round(_sp * 2.2); height: width; radius: width / 2
                        color: logPickerClose.containsMouse ? Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.12) : "transparent"
                        Text {
                            anchors.centerIn: parent; text: "✕"
                            color: qgcPal.buttonHighlightText; font.pixelSize: _sp
                        }
                        MouseArea {
                            id: logPickerClose
                            anchors.fill: parent
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: _showLogPicker = false
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "点击下载并分析日志"
                    color: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.5)
                    font.pixelSize: Math.round(_sp * 0.8)
                }

                // ── Log list ──
                ListView {
                    id: logPickerList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 1
                    model: _onboardLogList

                    delegate: Rectangle {
                        width: logPickerList.width
                        height: Math.round(_sp * 3.0)
                        radius: Math.round(_sp * 0.2)
                        color: lpHover.hovered ? Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.12) : qgcPal.windowShade
                        border.color: lpHover.hovered ? _accentCyan : qgcPal.groupBorder; border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Math.round(_sp * 0.5)
                            anchors.rightMargin: Math.round(_sp * 0.5)
                            anchors.topMargin: Math.round(_sp * 0.15)
                            anchors.bottomMargin: Math.round(_sp * 0.15)
                            spacing: Math.round(_sp * 0.5)

                            Text {
                                text: "#" + (modelData.index !== undefined ? modelData.index : modelData.id)
                                color: _accentCyan
                                font.pixelSize: Math.round(_sp * 0.9)
                                font.bold: true
                                Layout.preferredWidth: Math.round(_sp * 3)
                                verticalAlignment: Text.AlignVCenter
                            }
                            Text {
                                text: modelData.time || ""
                                color: qgcPal.buttonHighlightText
                                font.pixelSize: Math.round(_sp * 0.9)
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            Text {
                                text: modelData.size || ""
                                color: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.6)
                                font.pixelSize: Math.round(_sp * 0.85)
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        HoverHandler { id: lpHover }
                        TapHandler {
                            onTapped: {
                                var idx = modelData.index !== undefined ? modelData.index : 0
                                var ag = _agentMgr
                                if (ag) ag.downloadOnboardLogByIndex(idx)
                                _showLogPicker = false
                            }
                        }
                    }
                }

                // ── Analyze latest downloaded log ──
                Rectangle {
                    Layout.fillWidth: true
                    height: Math.round(_sp * 2.6)
                    radius: Math.round(_sp * 0.25)
                    color: latestHover.hovered ? Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.15) : Qt.rgba(_accentCyan.r, _accentCyan.g, _accentCyan.b, 0.08)
                    border.color: _accentCyan; border.width: 1
                    visible: _logListCache.length > 0

                    Text {
                        anchors.centerIn: parent
                        text: "📊 分析本地最新日志"
                        color: _accentCyan
                        font.pixelSize: Math.round(_sp * 0.9)
                        font.bold: true
                    }

                    HoverHandler { id: latestHover }
                    TapHandler {
                        onTapped: {
                            _showLogPicker = false
                            var ag = _agentMgr
                            if (ag) ag.analyzeLogByIndex(0)
                        }
                    }
                }
            }
        }
    }

    // ════════════════════════════════════════════════════
    //  C++ Connections
    // ════════════════════════════════════════════════════
    Connections {
        target: _agentMgr

        function onMessageReceived(role, content) {
            if (role === "assistant") {
                _removeThinkingItems()
                _removeStreamingItem()
            }
            chatModel.append({"role": role, "content": content, "ts": _nowTs()})
        }
        function onIsProcessingChanged() { _syncThinkingItem(); _syncStreamingItem() }
        function onThinkingTextChanged() { _syncThinkingItem() }
        function onStreamingTextChanged() { _syncStreamingItem() }
        function onCommandPending(description, cmdMap) {
            if (!_veh) return
            _pendingCmd = cmdMap; _pendingDesc = description
            _confirmSecondsLeft = 30; _showConfirm = true
        }
        function onCommandExecuted(result) { chatModel.append({"role": "system", "content": result, "ts": _nowTs()}) }
        function onCommandFailed(error) { chatModel.append({"role": "system", "content": "✗ " + error, "ts": _nowTs()}) }
        function onAutonomousStep(thought, action) {
            chatModel.append({"role": "system", "content": "[Agent] " + thought + " → " + action, "ts": _nowTs()})
        }
        function onModelsFetched(models) {
            var arr = []; for (var i = 0; i < models.length; i++) arr.push(models[i])
            _fetchedModels = arr
            // Only notify in chat when not on settings tab (settings has its own status indicator)
            if (currentTab !== 3) {
                chatModel.append({"role": "system", "content": "✓ 获取到 " + arr.length + " 个模型", "ts": _nowTs()})
            }
        }
        function onDownloadedLogsList(logs) { _logListCache = logs }
        function onOnboardDownloadProgress(status) {
            // "列表已加载" = log list just arrived → show picker
            // Any other status = download progress → show in chat, do NOT reopen picker
            if (status === "列表已加载") {
                if (_agentMgr && typeof _agentMgr.listOnboardLogs === 'function') {
                    var list = _agentMgr.listOnboardLogs()
                    if (list && list.length > 0) {
                        _onboardLogList = list
                        _showLogPicker = true
                    }
                }
            } else if (status && status.length > 0) {
                chatModel.append({"role": "system", "content": status, "ts": _nowTs()})
            }
        }
        function onOnboardDownloadComplete(filePath) {
            if (filePath) chatModel.append({"role": "system", "content": "✅ 日志下载完成，开始分析...", "ts": _nowTs()})
        }
        function onLogAnalysisReady(report) {
            // Auto-switch to Charts tab when analysis completes
            currentTab = 2
        }
        function onRagTestResult(message) {
            ragTestResultText.text = message
        }
    }
}
