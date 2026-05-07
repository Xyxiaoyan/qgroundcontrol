# AGENTS.md

Instructions for AI coding agents (Codex, Claude Code, etc.) working on QGroundControl.

## 开发记录 / Development Log

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
- QRC 加载的 QML 不能 import `QGroundControl.ScreenTools`/`Palette`（模块不可见）
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

**Key Principle**: Match the style of code you're editing. See [CODING_STYLE.md](CODING_STYLE.md) for conventions and [CODING_STYLE.md#examples](CODING_STYLE.md#examples) for canonical Vehicle/Fact/QML snippets.
