# AGENTS.md

Instructions for AI coding agents (Codex, Claude Code, etc.) working on QGroundControl.

## 开发记录 / Development Log

### 2026-07-02: API 配置完善 + 安全修复 (API Config Improvements)

**供应商预设选择器:**
- `custom/src/AgentPanel.qml` — 新增供应商 ComboBox（DeepSeek / DashScope / 智谱GLM / OpenAI / Ollama / 自定义），切换时自动填充 Base URL + 默认模型 + placeholder 提示
- `custom/src/AgentPanel.qml` — `_providerPresets` 只读属性定义 6 个供应商预设（baseUrl + defaultModel + keyHint）
- `custom/src/AgentPanel.qml` — `_detectProviderIndex()` 根据当前 baseApiEndpoint 反向检测供应商
- `custom/src/AgentPanel.qml` — `_applyProviderPreset()` 供应商切换时自动填充 URL/模型，若 Key 已填则自动 fetchModels

**Endpoint 字段改进:**
- `custom/src/AgentPanel.qml` — Endpoint 字段改为 "Base URL"，显示不含 `/v1/chat/completions` 的基础 URL，添加提示文字
- `src/Agent/AgentManager.h` — 新增 `Q_INVOKABLE baseApiEndpoint()` 返回去掉 `/chat/completions` 后缀的 URL
- `src/Agent/AgentManager.cpp` — 实现 `baseApiEndpoint()`

**Endpoint 规范化逻辑重写:**
- `src/Agent/AgentManager.cpp` — `setApiEndpoint()` 重写：先去尾部斜杠；若已含 `/chat/completions` 则去重后直接接受；否则用正则 `/v\d+$` 检测版本化路径（/v1, /v4 等）只追加 `/chat/completions`，其余追加 `/v1/chat/completions`
- `src/Agent/LLMClient.cpp` — `fetchModels()` URL 推导简化：统一去掉 `/chat/completions` 后缀后追加 `/models`，兼容 /v4 等非 /v1 路径

**输入延迟保存:**
- `custom/src/AgentPanel.qml` — API Key / Base URL / Geocode Key 字段 `onTextChanged` → `onEditingFinished`，避免逐字符触发加密+写磁盘
- `custom/src/AgentPanel.qml` — 所有 TextField 添加 `selectByMouse: true`

**调试日志安全修复:**
- `src/Agent/LLMClient.cpp` — `sendFunctionRequest()` 中 `qWarning` 完整请求体日志改为 `qCDebug(LLMClientLog)` 简要日志
- `src/Agent/LLMClient.cpp` — `_onReplyFinished()` 中 `qWarning` 完整响应体日志改为 `qCDebug(LLMClientLog)` 简要日志

### 2026-07-02: 日志分析全面改进 (Log Analysis Improvements)

基于用户反馈，对日志分析管道进行 8 项功能增强。

**DataFlash .bin 支持:**
- `src/Agent/LogAnalyzer.h` — 新增 `_extractDataFlashData()`、`_saveReport()` 方法声明，`_lastSavedReportPath` 成员变量，`lastSavedReportPath()` 公共方法
- `src/Agent/LogAnalyzer.cpp` — 实现 DataFlash 两轮解析：第一轮 `parseFmtMessages()` 学习消息格式定义，第二轮 `iterateMessages()` 提取数据
  - IMU: "IMU"/"IMU2" → AccX/Y/Z + GyrX/Y/Z → SensorSample
  - GPS: "GPS"/"GPS2" → Lat/Lng/Alt/HDop/NSats → GpsSample（自动检测并归一化 1e7 坐标）
  - Battery: "BAT"/"BATT" → Volt/Curr/Remaining → BatterySample（自动检测并归一化 centi-volt/amp）
  - EKF: "NKF1"/"XKF1" → IVN/IPN/IAN/IMX → EkfSample
  - `loadLog()`: ULog 头检查失败后回退到 DataFlash 头（`0xA3 0x95`）

**机载日志交互式下载:**
- `src/Agent/AgentManager.h` — 新增 `Q_INVOKABLE QJsonArray listOnboardLogs()`、`Q_INVOKABLE void downloadOnboardLogByIndex(int index)`
- `src/Agent/AgentManager.cpp` — `_onOnboardLogListComplete()` 不再自动下载，改为展示列表（序号/ID/时间/大小）；用户通过 `/download N` 命令选择条目下载

**UI 增强:**
- `custom/src/AgentPanel.qml` — 新增 "📂 选择日志" 按钮（FileDialog）、“📋 历史日志” 按钮（调用已有 `listDownloadedLogs()`）
- `custom/src/AgentPanel.qml` — 新增 `DropArea` 支持拖放日志文件自动分析
- `custom/src/AgentPanel.qml` — 新增 `/download N` 命令解析，选择机载日志条目
- `custom/src/AgentPanel.qml` — `import QtQuick.Dialogs` 添加 FileDialog 支持

**分析结果持久化:**
- `src/Agent/LogAnalyzer.cpp` — `_saveReport()` 将报告保存到 `~/.agentqgc/logs/analysis_<logname>_<timestamp>.txt`
- `src/Agent/LogAnalyzer.cpp` — `generateReport()` 末尾自动调用 `_saveReport()`，报告尾部追加 "📁 报告已保存: <path>"

### 2026-07-02: UI 生产级设计改造 + 参数工具修复

**UI 改造 (AgentPanel + FlyViewCustomLayer):**
- `custom/src/AgentPanel.qml` — 全面重写：100% QGCPalette 颜色语义化、统一 _sp 缩放、hover 高亮效果、状态栏/错误栏修复 Layout 混用、错误栏 Behavior 动画、聊天消息 add 过渡动画 (opacity+y)、确认弹窗 scale+opacity 动画
- `custom/src/FlyViewCustomLayer.qml` — AI 按钮匹配 QGC ToolStrip 风格：方形按钮、hover/pressed 状态、QGCPalette 语义色、Tooltip hover 显示

**ParameterTool 修复:**
- `src/Agent/Tools/ParameterTool.cpp` — `list_parameters` 移除硬编码别名表（8 组关键词），改为 LLM 自由指定任意参数名前缀/子串搜索，返回上限 30→50

**DJI Fly 组件移除:**
- `custom/src/FlyViewCustomLayer.qml` — 移除所有 5 个 DJIFly Loader
- `custom/custom.qrc` — 移除 DJIFly 资源条目
- `custom/res/DJIFly/` — 整目录删除

### 2026-06-30: PID 分析数学公式

**PID 计算实现在 `LogAnalyzer::_computePidStats()`：**

1. **RMS 跟踪误差**：
   ```
   trackingErrorRms = sqrt(mean((Actual - Setpoint)²))
   ```
   分别计算 Roll、Pitch、Yaw 三个轴。

2. **稳态误差**：
   取日志末尾 20% 样本的 tracking error 平均值的绝对值：
   ```
   steadyStateError = |mean(Actual - Setpoint)|  // last 20% samples
   ```

3. **最大误差**：追踪窗口内 `|Actual - Setpoint|` 的最大值。

4. **过冲检测**：
   当 `Actual > Setpoint * 1.3` 时计为过冲：
   ```
   overshootPct = overshootCount / totalSamples * 100
   ```
   （即实际值超过设定点 130% 的次数比例）

5. **振荡检测**：
   通过 tracking error 导数过零检测：
   ```
   error = Actual - Setpoint
   dError = error[t] - error[t-1]
   oscillationCount++  // when dError crosses zero (sign change)
   ```

6. **调节时间**：当前实现中为估算值（未精确计算 5% 稳态带），直接使用 0。

**振动分析计算：**
- **RMS**：`sqrt(mean(X²))`，阈值 < 0.5 (正常) / 0.5-1.5 (关注) / > 1.5 (异常)
- **FFT**：Cooley-Tukey 基-2 原位算法，4096 点窗口，Hann 窗预处理
- **峰值频率**：取 FFT 幅度谱最大值对应的频率（排除 DC 分量）
- **剪裁检测**：加速度瞬时值 > 156 m/s²（±16g 传感器量程）计为剪裁
- **频带能量**：低频 0-50Hz、中频 50-200Hz、高频 200-500Hz 的能量（幅度平方和）

**电池分析计算：**
- 电压降：负载电压最小值与空载电压最大值的差值
- 容量积分：`∑(Current * Δt)` 将安培秒转为 mAh
- 能量：`AverageVoltage * Capacity(Ah)` 单位 Wh
- C 倍率：`平均电流(A) / 容量(Ah)`

**GPS 分析计算：**
- HDOP 跳变检测：连续采样点间大圆圆距离 > 5m 计为位置跳变（可能为 GPS 干扰）
- HDOP 尖峰：`Max(HDOP) - Mean(HDOP)`
- 定位类型分布：统计无定位/2D/3D/RTK 的百分比

### 2026-06-29: 日志分析数据可视化 (Log Analysis Charts)

参考 [UAV-Insight-Toolkit](https://github.com/Tartistbz/UAV-Insight-Toolkit) 的 Plotly 图表设计，使用 Qt Graphs 实现飞行日志分析数据可视化。

**新增文件:**
- `src/Agent/LogAnalysisModel.h/.cpp` — 日志分析视图模型，暴露 Q_PROPERTY 给 QML（summary stats + time-series + flightModes）
- `custom/src/charts/LogChartView.qml` — 图表仪表盘容器，6 个子标签页切换
- `custom/src/charts/VibrationChart.qml` — 3 轴加速度震动时间序列，15/30 m/s² 阈值线
- `custom/src/charts/BatteryChart.qml` — 电压/电流/电量时间序列
- `custom/src/charts/GpsChart.qml` — HDOP + 卫星数时间序列，1.5 阈值线
- `custom/src/charts/EkfChart.qml` — EKF 创新测试比 (vel/pos/hgt/mag)，0.5 故障阈值
- `custom/src/charts/PidChart.qml` — PID 角速度 Desired vs Actual 叠加图，Roll/Pitch/Yaw 选择 + MAE 指标
- `custom/src/charts/OpticalFlowChart.qml` — 光流质量 + Flow X/Y 双面板图
- `custom/src/charts/FlightModeOverlay.qml` — 可复用飞行模式背景色块（半透明矩形 + 模式名称）

**修改文件:**
- `src/Agent/LogAnalyzer.h` — 新增 `TimeSeriesData` 结构体和 `timeSeriesJson()` 方法声明
- `src/Agent/LogAnalyzer.cpp` — 实现 `timeSeriesJson()`：将原始样本序列化为 QJsonArray，含步长降采样（max 2000 点）
- `src/Agent/AgentManager.h` — 新增 `Q_PROPERTY(LogAnalysisModel* logAnalysisModel)` + `logAnalysisModelChanged` 信号
- `src/Agent/AgentManager.cpp` — `runLogAnalysis()` 中创建/填充 LogAnalysisModel，暴露结构化数据给 QML
- `src/Agent/CMakeLists.txt` — 添加 LogAnalysisModel.cpp/.h
- `custom/src/AgentPanel.qml` — 新增第 4 个标签页 "📊 图表"，Loader 加载 LogChartView，分析完成后自动切换
- `custom/custom.qrc` — 注册 8 个图表 QML 文件

**架构要点:**
- `LogAnalysisModel` 是独立的 QObject 视图模型，持有最新分析结果。LogAnalyzer 是无状态工作器，模型是有状态视图层
- 时间序列数据格式：`{t, x, y, z}` (加速度/陀螺仪)、`{t, v, a, pct}` (电池) 等，t 为相对秒数
- `subsample()` 方法支持步长降采样，QML 端进一步降采样到 800 点以保渲染流畅
- 图表使用 Qt Graphs（GraphsView + LineSeries + ValueAxis），暗/亮主题跟随 `qgcPal.globalTheme`
- FlightModeOverlay 使用半透明 Rectangle（alpha 0.08），颜色映射参考 UAV-Insight-Toolkit
- QRC 加载限制仍适用：不能 import QGroundControl.ScreenTools

### 2026-05-05: AI 调试助手 v1.0 (AgentPanel)

**新增文件:**
- `custom/src/AgentPanel.qml` — AI 调试助手面板，自包含 QML 组件
  - 纯 QML 实现，零 C++ 依赖，通过 QRC + `Qt.createComponent` 动态加载
  - 支持 OpenAI 兼容 API（通义千问/DeepSeek/智谱GLM/GPT）
  - 5 个快捷诊断指令（飞前检查/电池/GPS/震动/参数）
  - 自动注入飞行器上下文（固件/类型/模式/解锁/电池/GPS）作为 system prompt
  - API 配置持久化：`QtQuick.LocalStorage` SQLite 存储
  - 15s 请求超时 + 取消按钮 + 错误提示
  - 飞行器状态实时指示条（绿色已连接/红色无连接）
- `custom/src/FlyViewCustomLayer.qml` — 飞行视图覆盖层
  - 右上角 AI 按钮 + 错误状态可视化提示

**修改文件:**
- `src/FlyView/FlyView.qml` — Loader 加载 CustomOverlay
- `custom/CMakeLists.txt` — CUSTOM_DEFINITIONS QT_DEBUG（启用 Mock Link）
- `custom/custom.qrc` — 注册 AgentPanel.qml + CustomOverlay.qml

**Mock Link 启用:**
- `src/Comms/MockLink/CMakeLists.txt` — 移除 Debug-only return()
- `src/QmlControls/QGroundControlQmlGlobal.cc` — 移除 `#ifdef QT_DEBUG`，MockLink.h 无条件引入
- `src/UI/AppSettings/pages/SettingsPages.json` — Mock Link 始终可见
- `src/Comms/LinkConfiguration.h` — TypeMock 保持 QT_DEBUG 守卫（全局 QT_DEBUG 已启用）
- `src/Comms/MockLink/MockLinkMissionItemHandler.cc` — 修复 `-Werror` 下 itemCount 未初始化

**关键技术要点:**
- QRC 加载的 QML 不能 import `QGroundControl.ScreenTools`（模块不可见，会导致整个组件加载失败）。**可用**：`QGroundControl.globalPalette`（颜色）。**缩放方案**：用 `Text { font.pointSize: 10; readonly property real fontPixelSize: Math.round(contentHeight) }` 测量
- **数据读取**：用 QML 属性绑定（`readonly property var _xx: _veh.xxx.rawValue`），不能用 JS try/catch 读 Fact — JS 上下文里 Fact 属性不解析
- **所有 Fact 读取**通过 `_rv(fact, def)` 封装 try-catch
- **QML 语法严格**：多行格式，禁用对象闭包后跟 `;`
- 电池访问路径：`_veh.batteries.get(0).percentRemaining.rawValue`（不是 `.battery.percent`）
- 飞行模式是 QString Q_PROPERTY（不是 Fact）
- Mock Link 不在 Release 构建可用（需 QT_DEBUG + CMake guard + Settings visibility）

### 2026-05-03: 高德地图集成 (GaoDe/Amap Provider)

**新增文件:**
- `src/QtLocationPlugin/Providers/GaoDeProvider.h` — 高德地图 Provider 声明
- `src/QtLocationPlugin/Providers/GaoDeProvider.cpp` — 瓦片 URL 实现

**修改文件:**
- `src/QtLocationPlugin/QGCMapUrlEngine.cpp` — `UrlFactory::_providers` 注册 `GaoDeRoadProvider`, `GaoDeSatelliteProvider`
- `src/QtLocationPlugin/CMakeLists.txt` — 添加 GaoDeProvider 源文件

**瓦片 URL:**
- 道路: `https://webrd0{1-4}.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x={x}&y={y}&z={z}`
- 卫星: `https://webst0{1-4}.is.autonavi.com/appmaptile?style=6&x={x}&y={y}&z={z}`

**架构说明:** 地图 Provider 继承 `MapProvider` 基类，实现 `_getURL()` 虚函数。注册只需在 `UrlFactory::_providers` 静态列表中添加 `std::make_shared<XxxProvider>()`，QGC 会自动发现并通过 Settings → General → Map Provider 出现在下拉列表中。

### 2026-06-24: UI 全面修复 (P0-P2)

**修改文件:**
- `custom/src/FlyViewCustomLayer.qml` — 重构：加载 DJIFly 5 组件、修复 z-order、移除 ScreenTools 依赖
- `custom/src/AgentPanel.qml` — 颜色改用 `QGroundControl.globalPalette`、字号改用 `_sp` 缩放、Timer 轮询改响应式绑定、确认按钮颜色修正、ComboBox 双触发修复、z-order 修正、临时对象 try/finally 修复
- `custom/res/DJIFly/DJIFlyTopBar.qml` — 电池路径修正 `vehicle.batteries.get(0).percentRemaining`、flightMode 修正（QString 非 Fact）、Row→RowLayout
- `custom/res/DJIFly/DJIFlyBottomBar.qml` — 视频 API 修正 `cameraManager.currentCameraInstance.toggleVideoRecording()`
- `custom/res/DJIFly/DJIFlyAttitude.qml` — 移除无效 `import QGroundControl.ScreenTools`
- `custom/res/DJIFly/DJIFlyTelemetry.qml` — 移除无效 `import QGroundControl.ScreenTools`
- `custom/custom.qrc` — 移除未使用的 4 个 delegate 文件注册

**删除文件:**
- `custom/src/delegates/MessageBubble.qml` — 未使用（AgentPanel 用内联 delegate）
- `custom/src/delegates/ToolCallCard.qml`
- `custom/src/delegates/StreamingText.qml`
- `custom/src/delegates/ThinkingBlock.qml`

**关键修复:**
- **ScreenTools 限制确认**：QRC 加载的 QML `import QGroundControl.ScreenTools` 会导致组件加载失败（warning "module is not installed"）。改用 Text 测量法获取缩放因子
- **电池路径**：`vehicle.battery.percent` → `vehicle.batteries.get(0).percentRemaining`
- **flightMode**：`.value` → 直接访问（是 QString Q_PROPERTY 不是 Fact）
- **视频录制**：`videoStreamManager().toggleVideoRecord()` → `cameraManager.currentCameraInstance.toggleVideoRecording()`
- **DJI Fly 组件集成**：通过 Loader + vehicle 属性绑定加载 TopBar/BottomBar/Telemetry/Attitude/MiniMap
- **z-order**：AI/截图按钮 `z: _highZ`，DJI 组件 `z: _highZ - 1`
- **响应式绑定**：2s Timer 轮询 → `readonly property` 直接绑定
- **确认弹窗**：确认按钮改绿色（非红色），`z: QGroundControl.zOrderTopMost`
- **ComboBox**：`onEditTextChanged`+`onActivated` 双触发 → `onActivated`+`onAccepted`
- **临时对象**：Canvas/TextEdit 添加 `try/finally { destroy() }`

### 2026-05-03: DJI Fly 风格自定义 UI 叠加层

**新增文件:**
- `custom/CMakeLists.txt` — 自定义构建配置
- `custom/custom.qrc` — 资源文件映射
- `custom/cmake/CustomOverrides.cmake` — 应用名称 "DJIFlyControl"
- `custom/src/FlyViewCustomLayer.qml` — DJI Fly 风格飞行视图覆盖层（自包含）

**机制:** QGC 检测到 `custom/` 目录后自动启用 `QGC_CUSTOM_BUILD`。`FlyViewCustomLayer.qml` 是 QGC 预留的 UI 覆盖层插槽，叠加在原生 Fly View 之上，访问真实车辆数据（`QGroundControl.multiVehicleManager.activeVehicle`）。

## Quick References

- [CODING_STYLE.md](CODING_STYLE.md) — Naming, formatting, C++20 features, QML style, logging
- [.github/CONTRIBUTING.md](.github/CONTRIBUTING.md) — Architecture patterns (Fact System, Multi-Vehicle, FirmwarePlugin)
- [tools/README.md](tools/README.md) — Development scripts and tooling
- [test/TESTING.md](test/TESTING.md) — Test framework, base classes, CTest labels, MultiSignalSpy, coverage
- [.pre-commit-config.yaml](.pre-commit-config.yaml) — All enforced linters (clang-format, clang-tidy, ruff, pyright, shellcheck, actionlint, zizmor, qmllint, clazy, vehicle-null-check, check-no-qassert, check-no-qtest-ignore-message)

## Review Process

Your output will be reviewed by another AI agent before being accepted. Write code and commit messages that are easy to machine-review: keep changes focused and minimal, use clear naming, and leave explanatory commit messages. Avoid unrelated changes, commented-out code, or ambiguous TODOs.

## Critical Files (Read First!)

1. `src/FactSystem/Fact.h` - Parameter system foundation
2. `src/Vehicle/Vehicle.h` - Core vehicle model
3. `src/FirmwarePlugin/FirmwarePlugin.h` - Firmware abstraction

## Build & Test Commands

- Recommended workflow: [tools/README.md](tools/README.md) Quick Start (`just configure / build / test / lint / check`).
- Testing details (CTest labels, coverage, sanitizers): [test/TESTING.md](test/TESTING.md).
- CI Python script tests: [.github/ci-overview.md](.github/ci-overview.md#tests).

## Golden Rules

See [.github/CONTRIBUTING.md#architecture-patterns](.github/CONTRIBUTING.md#architecture-patterns) for the canonical list (Fact System, Multi-Vehicle null-check, FirmwarePlugin, QML integration) and [CODING_STYLE.md#common-pitfalls](CODING_STYLE.md#common-pitfalls) for the full pitfall list with code examples.

## Code Structure

```
src/
├── Vehicle/          # Vehicle state/comms
├── FactSystem/       # Parameter management
├── FirmwarePlugin/   # PX4/ArduPilot abstraction
├── AutoPilotPlugins/ # Vehicle setup UI
├── MissionManager/   # Mission planning
├── MAVLink/          # Protocol handling
├── QmlControls/      # Reusable QML components
└── Settings/         # Persistent settings
```

## CI Structure

See [.github/ci-overview.md](.github/ci-overview.md) for the workflow/action/script layout and CI conventions (dependencies, shared helpers, bootstrap scripts, build config, GitHub Actions output).

---

<<<<<<< HEAD
**Key Principle**: Match the style of code you're editing. See [CODING_STYLE.md](CODING_STYLE.md) for conventions and [CODING_STYLE.md#examples](CODING_STYLE.md#examples) for canonical Vehicle/Fact/QML snippets.
=======
**Key Principle**: Match the style of code you're editing. See CODING_STYLE.md for conventions.


┌─────────────────────────────────────────────────┐
│  QML UI (custom/src/AgentPanel.qml)              │
│  聊天面板 / 快捷诊断 / 指令确认 / 自主模式开关      │
└────────────────────┬────────────────────────────┘
                     │ Q_PROPERTY + Connections
┌────────────────────▼────────────────────────────┐
│  AgentManager (单例门面)                          │
│  编排所有子组件，暴露 QML API                      │
└────┬──────┬──────┬──────┬──────┬────────────────┘
     │      │      │      │      │
┌────▼─┐ ┌─▼──┐ ┌─▼──┐ ┌─▼──┐ ┌─▼──────────┐
│ReAct │ │LLM │ │Tool│ │Ctx │ │ConfigStore  │
│Loop  │ │Clnt│ │Reg │ │Prov│ │ConvHistory  │
│      │ │    │ │    │ │    │ │LogAnalyzer  │
│      │ │    │ │    │ │    │ │LogAnlyModel │← QML图表数据
└──┬───┘ └────┘ └──┬─┘ └────┘ └─────────────┘
   │               │
   │  9 个 Tool ───┘
   │  VehicleContext / FlightControl / RelativeMove
   │  AltitudeSpeed / FlightMode / Parameter×3
   │  MavlinkCommand / LogAnalysis
>>>>>>> 42d63c9 (feat: AI Agent v2.0 - ReAct loop, log analysis, multi-provider LLM, dual-axis charts)
