# 配置说明

## 配置文件

Loong AI NVR 使用 JSON 格式配置文件。启动时通过命令行参数指定：

```bash
./loong-ainvr /path/to/config.json
```

若不指定，使用内置默认值。

---

## 完整配置模板

```json
{
  "system": {
    "name": "Loong AI NVR",
    "version": "0.1.0",
    "log_level": "info"
  },
  "auth": {
    "user_db": "users.db",
    "jwt_secret": "your-secret-key-at-least-32-bytes-long",
    "jwt_expiry_seconds": 86400
  },
  "network": {
    "http_host": "0.0.0.0",
    "http_port": 8080,
    "flv_port": 8081
  },
  "channel": {
    "max_channels": 64,
    "auto_recovery": true,
    "recovery_interval_sec": 5
  },
  "ai": {
    "default_backend": "opencv_dnn",
    "default_model": "yolov8n",
    "confidence_threshold": 0.5,
    "nms_threshold": 0.45,
    "analysis_fps": 5,
    "batch_size": 8,
    "batch_timeout_ms": 10
  },
  "storage": {
    "base_path": "/recordings",
    "index_db": "/recordings/index.db",
    "segment_duration_sec": 1800,
    "max_days": 30,
    "max_size_gb": 100,
    "cleanup_policy": "circular"
  },
  "pipeline": {
    "input_threads": 4,
    "decode_threads": 8,
    "ai_threads": 4,
    "overlay_threads": 4,
    "output_threads": 4,
    "queue_capacity": 128
  }
}
```

---

## 参数详解

### system — 系统基本信息

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `name` | string | `"Loong AI NVR"` | 系统名称 |
| `version` | string | `"0.1.0"` | 系统版本 |
| `log_level` | string | `"info"` | 日志级别: `trace` \| `debug` \| `info` \| `warn` \| `error` \| `critical` |

### auth — 认证配置

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `user_db` | string | `"users.db"` | 用户数据库路径 (SQLite) |
| `jwt_secret` | string | `"loong-ainvr-default-secret-change-me"` | JWT 签名密钥 (**生产环境必须修改**) |
| `jwt_expiry_seconds` | int64 | `86400` | Token 有效期 (秒)，默认 24 小时 |

> **安全提醒**: `jwt_secret` 应使用至少 32 字节的随机字符串。默认值仅供开发使用。

**生成安全密钥：**

```bash
openssl rand -hex 32
```

### network — 网络服务

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `http_host` | string | `"0.0.0.0"` | HTTP 监听地址 |
| `http_port` | int | `8080` | REST API 端口 |
| `flv_port` | int | `8081` | HTTP-FLV 流媒体端口 |

### channel — 通道管理

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `max_channels` | int | `64` | 最大通道数 |
| `auto_recovery` | bool | `true` | 异常通道自动恢复 |
| `recovery_interval_sec` | int | `5` | 恢复检测间隔 (秒) |

### ai — AI 分析引擎

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `default_backend` | string | `"opencv_dnn"` | 推理后端: `opencv_dnn` \| `onnxruntime` \| `tensorrt` |
| `default_model` | string | `"yolov8n"` | 默认 YOLO 模型 |
| `confidence_threshold` | float | `0.5` | 检测置信度阈值 |
| `nms_threshold` | float | `0.45` | NMS 非极大值抑制阈值 |
| `analysis_fps` | int | `5` | 分析帧率 (从视频流中每秒取几帧做 AI 分析) |
| `batch_size` | int | `8` | 批量推理大小 |
| `batch_timeout_ms` | int | `10` | 批量凑满超时 (毫秒) |

### storage — 存储管理

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `base_path` | string | `"/recordings"` | 录像文件根目录 |
| `index_db` | string | `"/recordings/index.db"` | 录像索引数据库路径 |
| `segment_duration_sec` | int | `1800` | 录像分段时长 (秒)，默认 30 分钟 |
| `max_days` | int | `30` | 录像保留天数 |
| `max_size_gb` | int | `100` | 最大存储空间 (GB) |
| `cleanup_policy` | string | `"circular"` | 清理策略: `circular` (循环覆盖) \| `stop_when_full` (满时停止) |

### pipeline — Pipeline 线程配置

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `input_threads` | int | `4` | 输入阶段线程数 |
| `decode_threads` | int | `8` | 解码阶段线程数 |
| `ai_threads` | int | `4` | AI 推理阶段线程数 |
| `overlay_threads` | int | `4` | 叠加阶段线程数 |
| `output_threads` | int | `4` | 输出阶段线程数 |
| `queue_capacity` | int | `128` | 阶段间队列容量 (帧数) |

---

## 配置建议

### 嵌入式设备 (ARM64, 4GB RAM)

```json
{
  "channel": { "max_channels": 16 },
  "ai": {
    "analysis_fps": 2,
    "batch_size": 4
  },
  "pipeline": {
    "input_threads": 2,
    "decode_threads": 4,
    "ai_threads": 2,
    "overlay_threads": 2,
    "output_threads": 2,
    "queue_capacity": 64
  },
  "storage": {
    "segment_duration_sec": 1800,
    "max_size_gb": 50
  }
}
```

### 高性能服务器 (x86_64, 32GB RAM, GPU)

```json
{
  "channel": { "max_channels": 64 },
  "ai": {
    "default_backend": "tensorrt",
    "analysis_fps": 10,
    "batch_size": 16
  },
  "pipeline": {
    "input_threads": 8,
    "decode_threads": 16,
    "ai_threads": 8,
    "overlay_threads": 8,
    "output_threads": 8,
    "queue_capacity": 256
  },
  "storage": {
    "max_size_gb": 2000
  }
}
```

---

## 默认管理员账号

首次启动时，系统自动创建默认管理员：

- **用户名**: `admin`
- **密码**: `admin123`

> **安全提醒**: 首次登录后请立即修改默认密码。

---

## 环境变量

当前版本不支持环境变量覆盖配置。所有配置通过 JSON 文件提供。

---

## 数据目录

| 路径 | 说明 | 可配置 |
|------|------|--------|
| `users.db` | 用户数据库 (相对于工作目录) | `auth.user_db` |
| `/recordings/` | 录像文件目录 | `storage.base_path` |
| `/recordings/index.db` | 录像索引数据库 | `storage.index_db` |
| `loong-ainvr.log` | 日志文件 (工作目录) | — |

> 确保录像目录有足够磁盘空间，且进程有写入权限。
