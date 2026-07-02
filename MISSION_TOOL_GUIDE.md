# MissionTool 使用指南

## 功能概述

`MissionTool` 允许 AI Agent 通过标准 QGC `.plan` 文件格式创建和上传航点任务到飞行器。

## API 操作

### 1. add_takeoff - 设置起飞高度
```json
{
  "action": "add_takeoff",
  "altitude": 10
}
```
- 设置任务的起飞高度（默认 10m）
- 起飞项会自动作为任务的第一项

### 2. add_waypoint - 添加航点
```json
{
  "action": "add_waypoint",
  "latitude": 47.398,
  "longitude": 8.545,
  "altitude": 50,
  "yaw": 90,
  "acceptance_radius": 5,
  "hold_time": 3
}
```
**必需参数**：
- `latitude`: 纬度
- `longitude`: 经度

**可选参数**：
- `altitude`: 高度（米，相对于 home，默认 50）
- `yaw`: 偏航角（度，省略则使用飞控默认行为：朝向下一航点）
- `acceptance_radius`: 到达半径（米，0 = 飞控默认）
- `hold_time`: 悬停时间（秒，0 = 飞越）

### 3. remove_waypoint - 删除航点
```json
{
  "action": "remove_waypoint",
  "index": 0
}
```
- `index`: 航点索引（0-based）

### 4. clear - 清空所有航点
```json
{
  "action": "clear"
}
```

### 5. list - 列出当前航点
```json
{
  "action": "list"
}
```
返回：
```json
{
  "status": "success",
  "count": 3,
  "takeoff_alt": 10,
  "has_takeoff": true,
  "waypoints": [
    {
      "index": 0,
      "latitude": 47.398,
      "longitude": 8.545,
      "altitude": 50,
      "yaw": 90,
      "acceptance_radius": 5,
      "hold_time": 3
    }
  ]
}
```

### 6. upload - 上传任务到飞行器
```json
{
  "action": "upload"
}
```
- 生成标准 `.plan` 文件
- 通过 QGC 原生管道上传到飞行器
- 上传后需切换到 AUTO 模式执行任务

### 7. download - 从飞行器下载任务
```json
{
  "action": "download"
}
```
- 触发异步下载
- 下载完成后可在 Plan View 中查看

## 典型工作流程

```
1. clear                          # 清空旧任务
2. add_takeoff (altitude=10)      # 设置起飞高度
3. add_waypoint (lat, lon, alt)   # 添加航点 1
4. add_waypoint (lat, lon, alt)   # 添加航点 2
5. add_waypoint (lat, lon, alt)   # 添加航点 3
6. list                           # 检查航点列表
7. upload                         # 上传到飞行器
8. [切换到 AUTO 模式执行任务]
```

## 生成的 .plan 文件格式

工具生成的 `.plan` 文件完全符合 QGC 标准格式，包含：

```json
{
  "groundStation": "QGroundControl",
  "fileType": "Plan",
  "version": 1,
  "mission": {
    "version": 2,
    "firmwareType": 12,
    "vehicleType": 2,
    "plannedHomePosition": [lat, lon, alt],
    "items": [
      {
        "type": "SimpleItem",
        "command": 22,  // MAV_CMD_NAV_TAKEOFF
        "frame": 3,     // MAV_FRAME_GLOBAL_RELATIVE_ALT
        "params": [0, 0, 0, null, NaN, NaN, 10],
        "Altitude": 10,
        "AltitudeMode": 1,
        "AMSLAltAboveTerrain": null
      },
      {
        "type": "SimpleItem",
        "command": 16,  // MAV_CMD_NAV_WAYPOINT
        "frame": 3,
        "params": [hold, accept, 0, yaw, lat, lon, alt],
        "Altitude": 50,
        "AltitudeMode": 1,
        "AMSLAltAboveTerrain": null
      }
    ]
  },
  "geoFence": { "version": 2, "polygons": [], "circles": [] },
  "rallyPoints": { "version": 2, "points": [] }
}
```

## 技术实现

### 上传流程
```
Waypoint 列表
  ↓
生成 .plan JSON
  ↓
写入临时文件 (/tmp/qgc_agent_mission.plan)
  ↓
离线 PlanMasterController::loadFromFile()  ← 仅用于解析
  ↓
MissionController::sendItemsToVehicle(vehicle, visualItems)
  ↓
_convertToMissionItems() → 转换为 MissionItem 列表
  ↓
PlanManager::writeMissionItems() → MAVLink 协议上传
```

### 关键设计
- **离线解析**：用离线 `PlanMasterController` 加载 `.plan` 文件，避免触发异步车辆下载
- **直接上传**：通过 `MissionController::sendItemsToVehicle()` 静态方法直接发送到真实车辆
- **标准格式**：生成的 `.plan` 文件可在 QGC Plan View 中加载、编辑和重新上传

### 与 QGC UI 的一致性
- 使用与 Plan View "Upload" 按钮**完全相同的代码路径**
- 生成的任务在 Plan View 中可见
- 支持所有 QGC 标准航点参数

## 示例：AI Agent 对话

**用户**："创建一个正方形飞行任务，边长 100 米，高度 50 米"

**Agent 执行**：
```
1. mission(action=clear)
2. mission(action=add_takeoff, altitude=10)
3. mission(action=add_waypoint, lat=47.398, lon=8.545, alt=50)
4. mission(action=add_waypoint, lat=47.399, lon=8.545, alt=50)
5. mission(action=add_waypoint, lat=47.399, lon=8.546, alt=50)
6. mission(action=add_waypoint, lat=47.398, lon=8.546, alt=50)
7. mission(action=upload)
```

**结果**：任务已上传，用户切换到 AUTO 模式即可执行。
