# REST API 参考 (v0.3.0)

**Base URL**: `http://<host>:8080/api/`

所有 API 端点（除 `/api/auth/login`、`/api/health`、`/metrics` 外）均需在 HTTP Header 中携带 JWT Token：

```
Authorization: Bearer <token>
```

所有请求和响应均使用 `Content-Type: application/json`。

**端点总数**: 65+

---

## 认证接口

### POST /api/auth/login

用户登录，获取 JWT Token。**无需认证**。

**请求体：**

```json
{
  "username": "admin",
  "password": "admin123"
}
```

**成功响应 (200)：**

```json
{
  "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "user": {
    "id": 1,
    "username": "admin",
    "display_name": "Administrator",
    "role": "admin"
  }
}
```

**失败响应 (401)：**

```json
{ "error": "invalid credentials" }
```

---

### GET /api/auth/profile

获取当前登录用户信息。

**响应 (200)：**

```json
{
  "id": 1,
  "username": "admin",
  "display_name": "Administrator",
  "role": "admin",
  "created_at": 1739808000,
  "last_login": 1739808100
}
```

---

### PUT /api/auth/password

修改当前用户密码。

**请求体：**

```json
{
  "old_password": "admin123",
  "new_password": "newpass456"
}
```

**成功响应 (200)：**

```json
{ "success": true }
```

**失败响应 (401)：**

```json
{ "error": "incorrect old password" }
```

---

## 用户管理接口 (仅管理员)

### GET /api/users

列出所有用户。

**响应 (200)：**

```json
[
  {
    "id": 1,
    "username": "admin",
    "display_name": "Administrator",
    "role": "admin",
    "enabled": true,
    "created_at": 1739808000,
    "updated_at": 1739808000,
    "last_login": 1739808100
  }
]
```

---

### POST /api/users

创建新用户。

**请求体：**

```json
{
  "username": "operator1",
  "password": "pass123456",
  "display_name": "张三",
  "role": "operator"
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `username` | string | ✅ | 唯一用户名 |
| `password` | string | ✅ | 至少 6 个字符 |
| `display_name` | string | ❌ | 显示名称，默认同 username |
| `role` | string | ❌ | `admin` \| `operator` \| `viewer`，默认 `viewer` |

**成功响应 (201)：**

```json
{ "id": 2, "success": true }
```

**失败响应 (409)：**

```json
{ "error": "username already exists" }
```

---

### PUT /api/users/:id

更新用户信息。可选字段：不提供则保持不变。

**请求体：**

```json
{
  "display_name": "新名称",
  "role": "operator",
  "enabled": true,
  "password": "newpass"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `display_name` | string | 显示名称 |
| `role` | string | 角色 |
| `enabled` | boolean | 是否启用 |
| `password` | string | 新密码（留空则不修改） |

**成功响应 (200)：**

```json
{ "success": true }
```

---

### DELETE /api/users/:id

删除用户。不可删除最后一个管理员。

**成功响应 (200)：**

```json
{ "success": true }
```

**失败响应 (400)：**

```json
{ "error": "cannot delete user (may be the last admin)" }
```

---

## 通道管理接口

### GET /api/channels

列出所有通道。

**响应 (200)：**

```json
[
  {
    "id": 1,
    "name": "前门摄像头",
    "state": "Running",
    "frames_processed": 15000,
    "frames_dropped": 12
  }
]
```

---

### POST /api/channels

创建新通道。

**请求体：**

```json
{
  "name": "前门摄像头",
  "rtsp_url": "rtsp://192.168.1.100:554/stream1",
  "width": 1920,
  "height": 1080,
  "framerate": 25,
  "ai_model": "yolov8n"
}
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `name` | string | `""` | 通道名称 |
| `rtsp_url` | string | `""` | RTSP 流地址 |
| `width` | int | `1920` | 视频宽度 |
| `height` | int | `1080` | 视频高度 |
| `framerate` | int | `25` | 帧率 |
| `ai_model` | string | `""` | AI 模型名称 (空则不启用 AI) |

**成功响应 (201)：**

```json
{ "id": 1, "success": true }
```

---

### GET /api/channels/:id

获取单个通道详细状态。

**响应 (200)：**

```json
{
  "id": 1,
  "name": "前门摄像头",
  "state": "Running",
  "fps_in": 25,
  "fps_decode": 25,
  "fps_ai": 5,
  "frames_processed": 15000,
  "frames_dropped": 12,
  "error_message": ""
}
```

---

### PUT /api/channels/:id

修改通道配置。会执行 Stop → Reconfigure → Start。

**请求体：** 与 POST 相同。

**响应 (200)：**

```json
{ "success": true }
```

---

### DELETE /api/channels/:id

删除通道。会先停止运行中的通道。

**响应 (200)：**

```json
{ "success": true }
```

---

### POST /api/channels/:id/start

启动通道管线。

**响应 (200)：**

```json
{ "success": true }
```

---

### POST /api/channels/:id/stop

停止通道管线。

**响应 (200)：**

```json
{ "success": true }
```

---

## 录像接口

### GET /api/recordings

查询录像记录。

**查询参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `channel_id` | int | 通道 ID |
| `start` | int64 | 开始时间 (Unix ms) |
| `end` | int64 | 结束时间 (Unix ms) |

**示例：** `GET /api/recordings?channel_id=1&start=1739800000000&end=1739900000000`

**响应 (200)：**

```json
[
  {
    "id": 1,
    "channel_id": 1,
    "start_time": 1739808000000,
    "end_time": 1739809800000,
    "file_path": "/recordings/ch1/20260217_143000.mp4",
    "file_size": 104857600,
    "codec": "h264",
    "resolution": "1920x1080"
  }
]
```

---

## 事件接口

### GET /api/events

查询报警事件。

**查询参数：** 与 `/api/recordings` 相同。

**响应 (200)：**

```json
[
  {
    "id": 1,
    "channel_id": 1,
    "event_type": "person_detected",
    "event_time": 1739808500000,
    "confidence": 0.92,
    "metadata": "{\"class\":\"person\",\"bbox\":[100,200,300,400]}"
  }
]
```

---

## 系统接口

### GET /api/system/status

获取系统状态。

**响应 (200)：**

```json
{
  "version": "0.1.0",
  "channels_active": 5,
  "max_channels": 64
}
```

---

## 错误响应格式

所有错误统一返回：

```json
{ "error": "错误描述" }
```

| HTTP 状态码 | 含义 |
|------------|------|
| `200` | 成功 |
| `201` | 创建成功 |
| `400` | 请求参数错误 |
| `401` | 未认证 / Token 无效或过期 |
| `403` | 无权限 (角色不足) |
| `404` | 资源不存在 |
| `409` | 冲突 (如用户名重复) |
| `500` | 服务器内部错误 |

---

---

## 可观测性接口 (v0.2.0)

### GET /api/health

健康检查端点。**无需认证**。

**响应 (200)：**

```json
{
  "status": "healthy",
  "channels": { "total": 64, "running": 5, "error": 0 },
  "storage": { "total_gb": 100, "used_gb": 23.5 },
  "system": { "cpu_percent": 15.2, "memory_percent": 42.1, "uptime_seconds": 86400 }
}
```

---

### GET /metrics

Prometheus 指标端点。**无需认证**。返回 `text/plain; version=0.0.4` 格式。

---

## 流媒体接口 (v0.2.0)

### GET /live/ch{id}/index.m3u8

HLS 播放列表。需 `token` 查询参数。

### GET /live/ch{id}/{seq}.ts

HLS TS 切片。需 `token` 查询参数。

---

## 录像增强接口 (v0.2.0)

### GET /api/recordings/timeline

时间轴查询，返回片段及嵌入事件。

**查询参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `channel_id` | int | 通道 ID |
| `start` | int64 | 开始时间 (Unix ms) |
| `end` | int64 | 结束时间 (Unix ms) |

**响应 (200)：**

```json
{
  "channel_id": 1,
  "start_time": 1739808000000,
  "end_time": 1739894400000,
  "segments": [
    {
      "id": 1,
      "start_time": 1739808000000,
      "end_time": 1739809800000,
      "file_path": "/recordings/ch1/seg.mp4",
      "codec": "h264",
      "resolution": "1920x1080",
      "events": [
        { "id": 1, "event_type": "person", "event_time": 1739808500000, "confidence": 0.92 }
      ],
      "event_count": 1
    }
  ],
  "total_events": 1
}
```

---

### GET /api/events/search

AI 事件搜索。

**查询参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `channel_id` | int | 通道 ID |
| `start` | int64 | 开始时间 |
| `end` | int64 | 结束时间 |
| `event_type` | string | 事件类型 (person/vehicle/intrusion 等) |
| `min_confidence` | float | 最低置信度 (0-1) |

---

## 通知接口 (v0.2.0)

### GET /api/system/notifications

获取通知配置。**仅管理员**。

### PUT /api/system/notifications

更新 SMTP/Webhook 通知配置。**仅管理员**。

### POST /api/system/notifications/test

发送测试通知。

### GET /api/system/notifications/history

获取通知发送历史。

---

## 配置热更新接口 (v0.2.0)

### GET /api/system/config

获取当前运行时配置 (敏感字段已脱敏)。**仅管理员**。

### PUT /api/system/config

热更新配置。不可修改 `auth`、`tls`、`network` 部分。**仅管理员**。

**请求体：** 部分配置 JSON

```json
{
  "ai": { "confidence_threshold": 0.6, "analysis_fps": 10 },
  "hls": { "max_segments": 8 }
}
```

**响应 (200)：**

```json
{ "status": "ok", "message": "configuration updated" }
```

变更会通过 WebSocket `config_change` 主题广播。

---

## 错误响应格式

所有错误统一返回：

```json
{ "error": "错误描述" }
```

| HTTP 状态码 | 含义 |
|------------|------|
| `200` | 成功 |
| `201` | 创建成功 |
| `400` | 请求参数错误 |
| `401` | 未认证 / Token 无效或过期 |
| `403` | 无权限 (角色不足) |
| `404` | 资源不存在 |
| `409` | 冲突 (如用户名重复) |
| `429` | 请求过于频繁 (限流) |
| `500` | 服务器内部错误 |

---

---

## 智能分析规则接口 (v0.3.0)

### GET /api/rules

列出所有分析规则。

**查询参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `channel_id` | int | 按通道 ID 过滤 |
| `type` | string | 按类型过滤 (`cross_line` / `region_intrusion` / `object_counting` / `loitering`) |

**响应 (200)：**

```json
[
  {
    "id": 1,
    "name": "大门越线",
    "type": "cross_line",
    "channel_id": 1,
    "enabled": true,
    "params": {
      "line": {"x1": 0.2, "y1": 0.5, "x2": 0.8, "y2": 0.5},
      "bidirectional": true,
      "target_classes": ["person", "car"]
    },
    "schedule": {"start_time": "08:00", "end_time": "22:00"},
    "created_at": 1739808000,
    "updated_at": 1739808000
  }
]
```

---

### POST /api/rules

创建分析规则。**Operator+ 权限**。

**请求体：**

```json
{
  "name": "大门越线检测",
  "type": "cross_line",
  "channel_id": 1,
  "enabled": true,
  "params": {
    "line": {"x1": 0.2, "y1": 0.5, "x2": 0.8, "y2": 0.5},
    "bidirectional": true,
    "target_classes": ["person"],
    "confidence_threshold": 0.5
  }
}
```

| 类型 | params 字段 |
|------|------------|
| `cross_line` | `line`, `bidirectional`, `target_classes`, `confidence_threshold` |
| `region_intrusion` | `region` (多边形点数组), `target_classes`, `confidence_threshold` |
| `object_counting` | `line`, `target_classes`, `confidence_threshold` |
| `loitering` | `region`, `loiter_time_sec`, `cooldown_sec`, `target_classes` |

**成功响应 (201)：**

```json
{ "id": 1, "success": true }
```

---

### GET /api/rules/:id

获取单个规则详情（含 line/region/schedule 完整字段）。

---

### PUT /api/rules/:id

修改规则。**Operator+ 权限**。

---

### DELETE /api/rules/:id

删除规则。**Operator+ 权限**。

---

### GET /api/rules/events

查询规则触发事件。

**查询参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `channel_id` | int | 通道 ID |
| `start_time` | int64 | 开始时间 (Unix ms) |
| `end_time` | int64 | 结束时间 (Unix ms) |
| `limit` | int | 返回条数上限 (默认 100) |

**响应 (200)：**

```json
[
  {
    "id": 1,
    "rule_id": 1,
    "rule_name": "大门越线",
    "event_type": "cross_line",
    "channel_id": 1,
    "severity": "alarm",
    "details": {"direction": "A_to_B", "track_id": 5},
    "timestamp": 1739808500000
  }
]
```

---

## 数据分析接口 (v0.3.0)

### GET /api/analytics/summary

分析总览统计。

**查询参数：** `channel_id`, `start_time`, `end_time`

**响应 (200)：**

```json
{
  "total_events": 1500,
  "cross_line_events": 800,
  "intrusion_events": 200,
  "counting_total": 450,
  "loitering_events": 50,
  "active_channels": 5
}
```

---

### GET /api/analytics/trends

趋势数据。

**查询参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `channel_id` | int | 通道 ID |
| `granularity` | string | `hourly` / `daily` / `weekly` / `monthly` |
| `start_time` | int64 | 开始时间 |
| `end_time` | int64 | 结束时间 |

**响应 (200)：**

```json
{
  "granularity": "hourly",
  "data": [
    {"time": "2026-02-19T08:00:00Z", "count": 45},
    {"time": "2026-02-19T09:00:00Z", "count": 62}
  ]
}
```

---

### GET /api/analytics/heatmap

热力图数据。

**查询参数：** `channel_id`, `start_time`, `end_time`, `grid_size` (默认 20)

**响应 (200)：**

```json
{
  "grid_size": 20,
  "width": 20,
  "height": 20,
  "data": [[0, 5, 12, ...], [3, 8, 25, ...]]
}
```

---

### GET /api/analytics/peak-hours

24 小时高峰时段分析。

**查询参数：** `channel_id`, `start_time`, `end_time`

**响应 (200)：**

```json
{
  "hours": [
    {"hour": 0, "count": 5, "percentage": 0.3},
    {"hour": 8, "count": 120, "percentage": 8.0},
    {"hour": 12, "count": 200, "percentage": 13.3}
  ]
}
```

---

### GET /api/analytics/counting

实时计数统计。返回所有计数类型规则的当前计数。

**响应 (200)：**

```json
[
  {
    "rule_id": 3,
    "rule_name": "入口计数",
    "channel_id": 1,
    "count_a_to_b": 150,
    "count_b_to_a": 80,
    "net_count": 70
  }
]
```

---

## 车牌识别接口 (v0.3.0)

### GET /api/analytics/plates

车牌查询。

**查询参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `plate_number` | string | 车牌号码 (模糊搜索) |
| `channel_id` | int | 通道 ID |
| `start_time` | int64 | 开始时间 |
| `end_time` | int64 | 结束时间 |
| `limit` | int | 返回条数 (默认 50) |
| `offset` | int | 分页偏移 |

**响应 (200)：**

```json
{
  "plates": [
    {
      "id": 1,
      "plate_number": "京A12345",
      "plate_color": "blue_plate",
      "confidence": 0.95,
      "channel_id": 1,
      "timestamp": 1739808500000,
      "snapshot_path": "/recordings/ch1/plates/snap_001.jpg"
    }
  ],
  "count": 1
}
```

---

## 人脸分析接口 (v0.3.0)

### GET /api/analytics/faces

人脸查询。

**查询参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `channel_id` | int | 通道 ID |
| `gender` | string | `male` / `female` |
| `age_min` | int | 最小年龄 |
| `age_max` | int | 最大年龄 |
| `start_time` | int64 | 开始时间 |
| `end_time` | int64 | 结束时间 |
| `limit` | int | 返回条数 |
| `offset` | int | 分页偏移 |

**响应 (200)：**

```json
{
  "faces": [
    {
      "id": 1,
      "channel_id": 1,
      "gender": "male",
      "age": 35,
      "confidence": 0.92,
      "timestamp": 1739808600000
    }
  ],
  "count": 1
}
```

---

### POST /api/analytics/faces/search

Embedding 余弦相似度搜索。

**请求体：**

```json
{
  "embedding": [0.012, -0.034, 0.056, ...],
  "threshold": 0.7,
  "top_k": 10
}
```

**响应 (200)：**

```json
{
  "results": [
    {"face_id": 5, "similarity": 0.92, "channel_id": 1, "timestamp": 1739808600000}
  ]
}
```

---

## WebRTC 接口 (v0.3.0)

### POST /api/webrtc/offer

创建 WebRTC 会话。接收 SDP Offer，返回 SDP Answer。

**请求体：**

```json
{
  "channel_id": 1,
  "sdp": "v=0\r\no=- ..."
}
```

**响应 (200)：**

```json
{
  "session_id": "abc-123",
  "sdp": "v=0\r\no=- ...",
  "type": "answer"
}
```

---

### POST /api/webrtc/ice

添加远程 ICE Candidate。

**请求体：**

```json
{
  "session_id": "abc-123",
  "candidate": "candidate:1 1 UDP ...",
  "sdpMid": "0",
  "sdpMLineIndex": 0
}
```

---

### DELETE /api/webrtc/:session_id

关闭 WebRTC 会话。

---

## 告警管理接口 (v0.3.0)

### GET /api/alarms/rules

列出告警规则。**Admin 权限**。

### POST /api/alarms/rules

创建告警规则。

### PUT /api/alarms/rules/:id

更新告警规则。

### DELETE /api/alarms/rules/:id

删除告警规则。

### GET /api/alarms

查询告警记录。

**查询参数：** `channel_id`, `start_time`, `end_time`, `severity`, `acknowledged`, `limit`, `offset`

### PUT /api/alarms/:id/acknowledge

确认告警。

### GET /api/alarms/stats

告警统计摘要。

---

## AI 模型插件接口 (v0.3.0)

### GET /api/plugins

列出已安装的 AI 模型插件。

**响应 (200)：**

```json
[
  {
    "name": "custom-detector",
    "version": "1.0.0",
    "api_version": 1,
    "description": "Custom object detector",
    "loaded": true
  }
]
```

---

### POST /api/plugins/upload

上传 .so 插件文件。**Admin 权限**。

**Content-Type**: `multipart/form-data`

---

## MQTT 接口 (v0.3.0)

### GET /api/mqtt/config

获取 MQTT 配置。**Admin 权限**。

### PUT /api/mqtt/config

更新 MQTT 配置。**Admin 权限**。

**请求体：**

```json
{
  "broker_host": "mqtt.example.com",
  "broker_port": 1883,
  "tls_enabled": false,
  "event_topic": "loong/events",
  "command_topic": "loong/commands",
  "qos": 1
}
```

### POST /api/mqtt/test

测试 MQTT 连接。

---

## 云备份接口 (v0.3.0)

### GET /api/backup/config

获取云备份配置。**Admin 权限**。

### PUT /api/backup/config

更新云备份配置 (S3/MinIO/OSS)。**Admin 权限**。

**请求体：**

```json
{
  "enabled": true,
  "provider": "s3",
  "endpoint": "s3.amazonaws.com",
  "bucket": "loong-backup",
  "access_key": "...",
  "secret_key": "...",
  "policy": "event_only",
  "retention_days": 30
}
```

### GET /api/backup/status

获取备份状态 (已备份文件数、总大小、最后备份时间)。

### POST /api/backup/test

测试云存储连接。

### POST /api/backup/trigger

手动触发一次备份。

---

## CORS

服务器支持跨域请求：

```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
```

OPTIONS preflight 请求返回空 200 响应。
