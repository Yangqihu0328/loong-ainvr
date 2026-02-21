# 系统架构 (v0.3.0)

## 总览

Loong AI NVR 采用**模块化分层 Pipeline 架构**，将系统分为 8 层，14 个组件。各层通过事件总线和统一接口松耦合连接，支持独立开发、测试和部署。

```
┌──────────────────────────────────────────────────────────────┐
│                    表示层 (Presentation)                       │
│ Web UI: Dashboard | Live | Recordings | Rules | Analytics    │
│         LicensePlates | FaceAnalytics | MapView | Channels   │
│         Events | Users | Settings | Plugins                  │
│ (Vue.js 3 + Element Plus + ECharts + Leaflet.js + PWA)       │
├──────────────────────────────────────────────────────────────┤
│                    认证层 (Authentication)                     │
│ JWT (HMAC-SHA256) + RBAC (Admin / Operator / Viewer)         │
│ HTTPS/TLS + Rate Limiting + Login Lockout + Security Headers │
├──────────────────────────────────────────────────────────────┤
│                    接口层 (API Gateway)                        │
│ HTTP(S) REST API (65+ 端点) + WebRTC + FLV + HLS + RTSP + WS│
├──────────┬──────────┬──────────┬─────────────────────────────┤
│ 通道管理  │ 存储管理  │ 系统管理  │ 告警+通知 │ 配置热更新    │
├──────────┴──────────┴──────────┴─────────────────────────────┤
│                    智能分析层 (Intelligence)        [v0.3.0]   │
│ RuleEngine (越线/区域/计数/徘徊) + IOU Tracker               │
│ ModelCascade (多模型级联: LPR + Face)                         │
│ AnalyticsAggregator (趋势/热力图/高峰)                        │
├──────────────────────────────────────────────────────────────┤
│                    处理层 (Processing)                         │
│ Pipeline: Input → Decode → AI → Rule → Overlay → Output      │
├──────────────────────────────────────────────────────────────┤
│                    引擎层 (Engines)                            │
│ FFmpeg Codec + OpenCV DNN + YOLO Adapters + PluginManager    │
│ LPR (YOLO+CRNN) + Face (SCRFD+ArcFace)                      │
├──────────────────────────────────────────────────────────────┤
│                    持久层 (Persistence)                        │
│ MP4 Recording + SQLite WAL (segments/events/rules/plates/    │
│ faces/analytics/alarms/users) + CloudBackupService (S3/OSS)  │
├──────────────────────────────────────────────────────────────┤
│                    基础层 (Core)                               │
│ ThreadPool | EventBus | MemPool | Config | Logger            │
├──────────────────────────────────────────────────────────────┤
│                    集成层 (Integration)             [v0.3.0]   │
│ MQTT (IoT 发布/订阅) + WebRTC (loong-rtc P2P 直播)           │
└──────────────────────────────────────────────────────────────┘
```

---

## 组件清单

### 14 个静态库

| 库名 | 目录 | 核心职责 |
|------|------|---------|
| `loong_core` | `src/core/` | 线程池、事件总线、内存池、配置、日志、Pipeline 框架 |
| `loong_codec` | `src/codec/` | FFmpeg H.264/H.265 编解码 + 工厂 |
| `loong_pipeline_stages` | `src/pipeline_stages/` | 6 阶段视频处理管线 (含 RuleStage) |
| `loong_ai_engine` | `src/ai_engine/` | OpenCV DNN 推理 + YOLO 适配器 + ModelCascade + LPR + Face |
| `loong_rules` | `src/rules/` | 规则引擎 + 评估器 (4 种) + IOU Tracker + RuleStore |
| `loong_analytics` | `src/analytics/` | 数据聚合 + AnalyticsStore + 趋势/热力图/高峰分析 |
| `loong_storage` | `src/storage/` | MP4 录像 + SQLite 索引 + 清理 |
| `loong_network` | `src/network/` | REST API + FLV + HLS + RTSP + WebSocket |
| `loong_channel` | `src/channel/` | 64 路通道生命周期管理 |
| `loong_overlay` | `src/overlay/` | OSD 叠加渲染 + 规则可视化 + 多通道合成 |
| `loong_video_input` | `src/video_input/` | ONVIF 设备发现 + PTZ 控制 |
| `loong_system` | `src/system/` | 用户认证 + JWT + RBAC + 通知 + 告警 + 插件 + 云备份 |

### 依赖关系

```
loong_core ─────────────────────────────────────────┐
   │                                                │
   ├── loong_codec                                  │
   │      │                                         │
   │      ├── loong_pipeline_stages                 │
   │      │                                         │
   │      └── loong_storage                         │
   │                                                │
   ├── loong_ai_engine ──→ (cascade, lpr, face)     │
   │                                                │
   ├── loong_rules ────→ (loong_ai_engine tracker)  │
   │                                                │
   ├── loong_analytics ─→ (SQLite)                  │
   │                                                │
   ├── loong_overlay ──→ (loong_rules 可视化数据)    │
   │                                                │
   ├── loong_channel ──→ (loong_core only)          │
   │                                                │
   ├── loong_system ───→ (SQLite + OpenSSL + MQTT)  │
   │                                                │
   └── loong_network ─→ (all above + WebRTC)        │
                                                    │
loong-ainvr (executable) ←──────────────────────────┘
```

---

## Pipeline 模型

每个视频通道运行一条独立的 **6 阶段**处理管线：

```
┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐
│  Input   │→ │  Decode  │→ │    AI    │→ │   Rule   │→ │ Overlay  │→ │  Output  │
│  Stage   │  │  Stage   │  │  Stage   │  │  Stage   │  │  Stage   │  │  Stage   │
│          │  │          │  │          │  │          │  │          │  │          │
│ RTSP拉流  │  │ FFmpeg   │  │ YOLO推理  │  │ 规则评估  │  │ 检测框叠加 │  │ 编码+录像 │
│ 帧采集    │  │ H.264/5  │  │ +级联推理  │  │ 越线/区域 │  │ 规则可视化│  │ FLV/RTC  │
└──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘
       │             │             │             │             │             │
       └─────────────┴─────────────┴─────────────┴─────────────┴─────────────┘
                              StageQueue (线程安全 + 背压丢帧)
```

### 关键特性

- **StageQueue**: 每两个阶段间通过线程安全队列连接，支持背压丢帧（保留关键帧）
- **独立线程**: 每个阶段可配置独立线程数
- **零拷贝**: 帧数据通过 `shared_ptr` 在管线间共享，避免拷贝
- **RuleStage**: AI 之后评估规则，生成 RuleEvent 并通过 EventBus 分发
- **级联推理**: AiStage 支持单引擎和 ModelCascade 两种模式

---

## AI 引擎架构

采用三层正交抽象设计 + 级联扩展：

```
┌────────────────────────────────────────────────────────┐
│                  InferenceEngine                        │  ← 高层接口
│   Detect(frame) → vector<Detection>                    │
├────────────┬───────────────────────────────────────────┤
│  Backend   │          Model Adapter                    │  ← 两个正交维度
│  (推理后端)  │          (模型适配器)                      │
├────────────┼───────────────────────────────────────────┤
│ OpenCV DNN │  YoloV5Adapter (v5/v7)                   │
│ ONNX RT*   │  YoloV8Adapter (v8~v11)                  │
│ TensorRT*  │  LprDetectorAdapter (车牌检测)             │
│ Plugin*    │  LprOcrAdapter (CRNN 车牌 OCR)            │
│            │  FaceDetectorAdapter (SCRFD/RetinaFace)   │
│            │  FaceAttributeAdapter (ArcFace embedding) │
└────────────┴───────────────────────────────────────────┘
        * 标记为可选/运行时加载

┌────────────────────────────────────────────────────────┐
│                  ModelCascade                  [v0.3.0] │
│   多模型编排器 (Primary → Crop → Parallel)              │
├────────────────────────────────────────────────────────┤
│ Step 1 (Primary): YOLO 全帧目标检测                      │
│ Step 2 (Crop):    LPR 车牌裁剪→OCR 识别                 │
│ Step 3 (Crop):    人脸裁剪→属性分析→512-d embedding       │
│ Step N (Parallel): 自定义插件模型并行推理                  │
└────────────────────────────────────────────────────────┘
```

- **Backend (推理后端)**: 执行模型推理，隐藏底层框架差异
- **Adapter (模型适配器)**: 处理不同模型的输入预处理和输出后处理
- **ModelCascade**: 编排多步骤推理，支持 ROI 裁剪和二次检测
- **PluginManager**: dlopen 动态加载第三方 .so 模型，运行时扩展

---

## 智能分析规则引擎

```
AnalysisResult (AI检测结果)
         │
         ▼
┌─────────────────────────────────────────────┐
│              RuleEngine                      │
│                                             │
│  ┌─────────────┐  ┌───────────────────────┐ │
│  │ IOU Tracker  │  │ Rule × N             │ │
│  │ 帧间目标追踪  │  │ (enabled per channel) │ │
│  └──────┬──────┘  └──────────┬────────────┘ │
│         │                    │              │
│         ▼                    ▼              │
│  Tracks + Trajectories  ──→  Evaluators    │
│                              │              │
│      ┌───────────────────────┼──────┐       │
│      ▼           ▼           ▼      ▼       │
│  CrossLine   Region      Counting  Loiter  │
│  越线检测    区域入侵     目标计数   徘徊检测│
│  (叉积法)   (射线法)    (双向累计) (停留阈值)│
│      │           │           │      │       │
│      └───────────┴───────────┴──────┘       │
│                    │                        │
│                    ▼                        │
│            RuleEvent[]                      │
│            (type, severity, details)        │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
            EventBus 分发
            ├── AlarmManager → 告警处理
            ├── AnalyticsAggregator → 数据聚合
            ├── NotificationManager → SMTP/Webhook/MQTT
            └── WebSocket → 前端实时推送
```

---

## 认证架构

```
客户端请求
    │
    ▼
┌─────────────┐    ┌──────────────┐    ┌──────────────┐
│ HTTP Server │───→│ RequiresAuth?│───→│ Auth         │
│             │    │ (白名单检查)   │ Y  │ Middleware   │
│             │    └──────┬───────┘    │ (JWT验证)     │
│             │           │ N          └──────┬───────┘
│             │           │                   │
│             │           ▼                   ▼
│             │    ┌──────────┐       ┌──────────────┐
│             │    │ 直接路由  │       │ HasPermission│
│             │    │ (login)  │       │ (RBAC检查)    │
│             │    └──────────┘       └──────┬───────┘
│             │                              │
│             │                    ┌─────────┼─────────┐
│             │                    ▼         ▼         ▼
│             │               ┌──────┐  ┌──────┐  ┌──────┐
│             │               │ 200  │  │ 401  │  │ 403  │
│             │               │ OK   │  │Unauth│  │Forbid│
│             │               └──────┘  └──────┘  └──────┘
└─────────────┘
```

### RBAC 权限矩阵

| 资源 | Admin | Operator | Viewer |
|------|:-----:|:--------:|:------:|
| `/api/auth/login` | 公开 | 公开 | 公开 |
| `/api/health`, `/metrics` | 公开 | 公开 | 公开 |
| `/api/auth/profile\|password` | ✅ | ✅ | ✅ |
| `/api/users` (CRUD) | ✅ | ❌ | ❌ |
| `/api/channels` (GET) | ✅ | ✅ | ✅ |
| `/api/channels` (POST/PUT/DELETE) | ✅ | ✅ | ❌ |
| `/api/channels/:id/start\|stop` | ✅ | ✅ | ❌ |
| `/api/rules` (GET) | ✅ | ✅ | ✅ |
| `/api/rules` (POST/PUT/DELETE) | ✅ | ✅ | ❌ |
| `/api/analytics/*` | ✅ | ✅ | ✅ |
| `/api/recordings\|events` | ✅ | ✅ | ✅ |
| `/api/webrtc/*` | ✅ | ✅ | ✅ |
| `/api/alarms` (query) | ✅ | ✅ | ✅ |
| `/api/alarms/rules` (CRUD) | ✅ | ❌ | ❌ |
| `/api/plugins\|mqtt\|backup` | ✅ | ❌ | ❌ |
| `/api/system/*` | ✅ | ❌ | ❌ |

---

## 通道状态机

```
         CreateChannel()
              │
              ▼
          ┌────────┐
          │Created │
          └───┬────┘
              │ Configure
              ▼
          ┌──────────┐
          │Configured│◄─────────────┐
          └───┬──────┘              │
              │ Start               │ Stop→Edit→Start
              ▼                     │
          ┌────────┐           ┌────────┐
          │Running │──Error──→│ Error  │
          └───┬────┘           └───┬────┘
              │ Stop               │ AutoRecovery
              ▼                     │
          ┌────────┐               │
          │Stopped │←──────────────┘
          └───┬────┘
              │ Delete
              ▼
          ┌──────────┐
          │Destroyed │
          └──────────┘
```

---

## 存储架构

```
视频帧 ──→ RecordWriter ──→ /recordings/ch{id}/
                │                  ├── 20260217_143000.mp4
                │                  ├── 20260217_150000.mp4
                │                  └── ...
                │
                ▼
          RecordIndex (SQLite WAL)
                │
                ├── segments 表: 文件路径、时间范围、大小
                ├── events 表: 事件类型、时间、置信度
                ├── rules 表: 规则定义 + rule_events 触发日志
                ├── plates 表: 车牌号码、颜色、通道、快照路径
                ├── faces 表: 性别、年龄、embedding (BLOB)
                ├── analytics_hourly 表: 按小时聚合统计
                ├── detection_positions 表: 检测位置密度 (热力图)
                └── quotas 表: 每通道配额配置
                │
                ├── StorageCleaner (按配额策略循环删除最旧录像)
                │
                └── CloudBackupService (S3/MinIO/OSS 定期备份)
```

---

## 事件驱动架构

```
                        EventBus (发布-订阅)
                              │
        ┌─────────┬───────────┼───────────┬──────────────┐
        │         │           │           │              │
        ▼         ▼           ▼           ▼              ▼
  ai.detection  rule.triggered  alarm.triggered  config_change
        │         │           │                        │
        ▼         ▼           ▼                        ▼
  PlateStore   Analytics   AlarmManager          HotReload
  FaceStore    Aggregator       │                 Handlers
  (入库)       (聚合)           ├─ NotificationManager
                               │   ├─ SmtpChannel
                               │   ├─ WebhookChannel
                               │   └─ MqttChannel
                               │
                               └─ WebSocket 广播
                                  (前端实时推送)
```

---

## 设计模式

| 模式 | 应用场景 |
|------|---------|
| **Pipeline** | 6 阶段视频处理管线 |
| **Factory** | 编解码器、YOLO 适配器、推理后端、规则评估器创建 |
| **Observer** | EventBus 发布-订阅组件通信 |
| **State Machine** | 通道生命周期状态转换 |
| **Strategy** | 存储清理策略、规则评估策略、云备份策略 |
| **Chain of Responsibility** | ModelCascade 多步骤级联推理 |
| **Plugin** | PluginManager dlopen 动态加载第三方模型 |
| **RAII** | FFmpeg 资源、文件句柄、帧内存管理 |
| **Middleware** | HTTP 认证拦截、限流、安全头注入 |
| **Singleton** | ConfigManager、EventBus、ThreadPool |
