# Loong AI NVR

[![CI](https://github.com/Yangqihu0328/loong-ainvr/actions/workflows/ci.yml/badge.svg)](https://github.com/Yangqihu0328/loong-ainvr/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/Yangqihu0328/loong-ainvr/graph/badge.svg)](https://codecov.io/gh/Yangqihu0328/loong-ainvr)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

**嵌入式智能网络视频录像机系统** — 将传统 NVR 全功能与 YOLO 系列 AI 视频分析深度融合，提供智能规则引擎、多模型级联推理、WebRTC 超低延迟直播、车牌/人脸识别等高级能力。

---

## 功能特性

### 核心能力
- **64 路并发接入** — 支持 64 路 RTSP 视频流同时接入、分析和叠加输出
- **YOLO 全版本 AI 分析** — 支持 YOLOv5 / v7 / v8 / v9 / v10 / v11，实时目标检测
- **完整 NVR 功能** — 录像、回放 (0.25x~8x 倍速)、时间轴可视化、存储管理、实时预览、报警管理
- **H.264 / H.265 编解码** — FFmpeg 软解码 + 硬件加速 (CUDA / VAAPI / QSV)

### 智能分析 (v0.3.0)
- **规则引擎** — 越线检测、区域入侵、目标计数、徘徊检测四种分析规则，IOU Tracker 帧间追踪
- **多模型级联推理** — ModelCascade 编排器，支持 Primary / Crop / Parallel 三种级联模式
- **车牌识别 (LPR)** — 两阶段识别：YOLO 检测 + CRNN OCR (68 字符中国车牌字符集)
- **人脸检测与属性** — SCRFD/RetinaFace 检测 + ArcFace 512-d embedding + 年龄/性别分类
- **数据分析仪表盘** — ECharts 可视化趋势、类型分布、热力图、高峰时段分析

### 流媒体与直播
- **多协议流媒体** — WebRTC (<200ms) / HTTP-FLV / HLS / RTSP / WebSocket，自动协议降级
- **WebRTC 超低延迟** — 基于 loong-rtc 的 P2P 直播，DTLS-SRTP 加密，ICE NAT 穿透

### 安全与运维
- **用户权限管理** — JWT 认证 + RBAC 三级角色 (管理员 / 操作员 / 观察者)
- **HTTPS/TLS** — TLS 加密传输，安全头、API 限流 (120 req/60s)、登录锁定 (5 次/15 分钟)
- **告警通知** — SMTP 邮件 + Webhook + MQTT 推送，事件驱动自动告警
- **可观测性** — 健康检查、Prometheus metrics、结构化 JSON 日志
- **配置热更新** — inotify 文件监听 + API 修改，无需重启即时生效

### 平台扩展
- **AI 模型插件系统** — dlopen 动态加载第三方 .so 模型，运行时扩展 AI 能力
- **MQTT 集成** — IoT 场景告警发布 + 命令订阅
- **云存储备份** — S3 / MinIO / 阿里云 OSS 兼容，定期自动备份录像和事件快照
- **GIS 地图视图** — Leaflet.js 摄像头 GPS 标注，实时状态着色，告警闪烁
- **PWA 支持** — 离线缓存 + Web Push 推送通知 + 移动端安装

### 部署
- **容器化部署** — Docker + docker-compose 一键启动，支持 NVIDIA GPU
- **响应式 Web UI** — Vue.js 3 + Element Plus，手机/平板/桌面自适应
- **嵌入式优化** — 零拷贝帧缓冲、内存池复用、面向 ARM64 / x86_64

## 技术栈

| 层级 | 技术 |
|------|------|
| 后端 | C++17, CMake, Google C++ Style |
| AI 引擎 | OpenCV DNN / ONNX Runtime / TensorRT, YOLO v5~v11, SCRFD, ArcFace, CRNN |
| 规则引擎 | RuleEngine + IOU Tracker (越线/区域/计数/徘徊) |
| 级联推理 | ModelCascade (Primary / Crop / Parallel 多模型编排) |
| 编解码 | FFmpeg (H.264 / H.265, CUDA/VAAPI/QSV 硬件加速) |
| 流媒体 | WebRTC (loong-rtc), HTTP-FLV, HLS (M3U8+TS), RTSP, WebSocket fMP4 |
| 存储 | SQLite (WAL 模式), MP4 分段录像, S3 兼容云备份 |
| 认证 | JWT (HMAC-SHA256), SHA-256 密码哈希, OpenSSL |
| Web UI | Vue.js 3, Element Plus, Pinia, Axios, flv.js, hls.js, ECharts, Leaflet.js |
| 可观测性 | spdlog (JSON), Prometheus metrics, Health check |
| 集成 | MQTT (paho-mqtt), Webhook (HMAC-SHA256), SMTP |
| CI/CD | GitHub Actions, Docker, clang-tidy, gcov/lcov, Codecov |
| 测试 | Google Test (250+ 用例), Google Benchmark |

## 快速开始

### 方式一：Docker 部署 (推荐)

```bash
# 标准部署
docker-compose up -d

# GPU 加速 (需要 NVIDIA Container Toolkit)
docker-compose -f docker-compose.gpu.yml up -d
```

### 方式二：源码编译

#### 1. 安装依赖

```bash
# Ubuntu 22.04+
bash scripts/install-deps.sh
```

#### 2. 构建后端

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLOONG_BUILD_TESTS=ON
cmake --build build --parallel $(nproc)
```

#### 3. 运行测试

```bash
cd build && ctest --output-on-failure
```

#### 4. 启动服务

```bash
./build/loong-ainvr config/default.json
```

#### 5. 构建前端

```bash
cd web && npm install && npm run dev
```

### 访问系统

| 服务 | 地址 |
|------|------|
| Web UI | http://localhost:3000 |
| HTTP API | http://localhost:8080/api/ |
| 健康检查 | http://localhost:8080/api/health |
| Prometheus | http://localhost:8080/metrics |
| FLV 直播 | http://localhost:8080/live/ch{id}.flv |
| HLS 直播 | http://localhost:8080/live/ch{id}/index.m3u8 |
| WebRTC 信令 | POST http://localhost:8080/api/webrtc/offer |
| WebSocket | ws://localhost:8081 |
| RTSP | rtsp://localhost:554/live/ch{id} |

默认管理员账号: `admin` / `admin123` (首次登录强制修改密码)

## 项目结构

```
loong-ainvr/
├── src/                        # C++ 后端源码
│   ├── core/                   # 核心框架 (ThreadPool/EventBus/MemPool/Config/Logger/Pipeline)
│   ├── codec/                  # FFmpeg H.264/H.265 编解码
│   ├── ai_engine/              # AI 推理引擎 (OpenCV DNN + ONNX Runtime + TensorRT)
│   │   ├── cascade/            # 多模型级联推理 (ModelCascade)
│   │   ├── lpr/                # 车牌识别 (LprDetector + LprOcr + PlateStore)
│   │   └── face/               # 人脸检测 (FaceDetector + FaceAttribute + FaceStore)
│   ├── rules/                  # 智能分析规则引擎
│   │   ├── rule_engine/        # 规则引擎 + 评估器 (CrossLine/Region/Counting/Loitering)
│   │   ├── rule_store/         # 规则存储 (SQLite)
│   │   └── tracker/            # IOU Tracker 目标追踪
│   ├── analytics/              # 数据分析聚合 (AnalyticsAggregator + AnalyticsStore)
│   ├── pipeline_stages/        # 6-Stage 视频处理管线 (含 RuleStage)
│   ├── overlay/                # OSD 叠加渲染 + 规则可视化 + 多通道合成
│   ├── storage/                # 存储管理 (MP4 录像 + SQLite 索引 + 清理)
│   ├── network/                # HTTP(S) API + FLV + HLS + RTSP + WebSocket + WebRTC
│   ├── channel/                # 64 路通道生命周期管理
│   ├── video_input/            # ONVIF 设备发现 + PTZ 控制
│   ├── system/                 # 认证 (JWT+RBAC) + 监控 + 通知 + 告警 + MQTT + 云备份 + 插件
│   └── main.cc                 # 系统入口 (21 个子系统初始化 + 优雅关闭)
├── web/                        # Vue.js 3 前端
│   └── src/
│       ├── views/              # Dashboard/Channels/LiveView/Recordings/Events/Users/Settings
│       │                       # Rules/Analytics/LicensePlates/FaceAnalytics/MapView/Plugins
│       └── components/         # NvrPlayer (WebRTC/FLV/HLS) + AnalyticsHeatmap + PwaPrompt
├── tests/                      # 单元测试 + 集成测试 + 基准测试
│   ├── unit/                   # 250+ 单元测试
│   ├── integration/            # 端到端集成测试
│   └── benchmark/              # 性能基准测试 (Google Benchmark)
├── config/                     # 配置文件模板
├── scripts/                    # 工具脚本 (deps, cert, icons, etc.)
├── docs/                       # 项目文档
├── .github/workflows/          # CI/CD 流水线
├── Dockerfile                  # 多阶段 Docker 构建
├── docker-compose.yml          # 标准部署
├── docker-compose.gpu.yml      # GPU 加速部署
└── Doxyfile                    # API 文档生成配置
```

## 系统架构

```
┌──────────────────────────────────────────────────────────────┐
│ Web UI (Vue.js 3 + Element Plus + ECharts + Leaflet.js)      │
│ Dashboard | Live | Recordings | Rules | Analytics | Map      │
│ LicensePlates | FaceAnalytics | Channels | Events | Settings │
├──────────────────────────────────────────────────────────────┤
│ Auth (JWT + RBAC) + HTTPS/TLS + Rate Limiting + Security     │
├──────────────────────────────────────────────────────────────┤
│ HTTP(S) API (65+ 端点) + WebRTC + FLV + HLS + RTSP + WS     │
├──────────┬──────────┬────────────┬───────────────────────────┤
│ 通道管理  │ 存储管理  │ 告警+通知   │ 规则引擎 │ 数据分析     │
│ 64ch     │ MP4+云备份│ SMTP/WH/MQ │ 4种规则  │ 聚合+热力图  │
├──────────┴──────────┴────────────┴───────────────────────────┤
│ Pipeline: Input → Decode → AI → Rule → Overlay → Output      │
├──────────────────────────────────────────────────────────────┤
│ AI Engine: YOLO + ModelCascade (LPR + Face) + Plugin System  │
├──────────────────────────────────────────────────────────────┤
│ Codec (FFmpeg) + Storage (MP4+SQLite) + ONVIF + WebRTC(RTC)  │
├──────────────────────────────────────────────────────────────┤
│ Core (ThreadPool | EventBus | MemPool | Config | Logger)     │
└──────────────────────────────────────────────────────────────┘
```

## 文档

- [系统架构](docs/architecture.md) — 分层架构、Pipeline 模型、规则引擎、级联推理设计
- [REST API 参考](docs/api-reference.md) — 全部 65+ 个端点详细说明
- [构建与部署](docs/build-guide.md) — 编译、Docker、交叉编译
- [配置说明](docs/configuration.md) — 配置文件参数详解
- [贡献指南](CONTRIBUTING.md) — 开发环境、代码规范、PR 流程
- [变更日志](CHANGELOG.md) — 版本历史

## 性能基准

使用 Google Benchmark 框架，包含以下基准测试：

- **Pipeline 吞吐量** — 单通道/多通道 (1/4/16/32ch) FPS
- **AI 推理延迟** — 单帧/批量 (1/2/4/8/16)，不同模拟延迟
- **内存池效率** — FrameBufferPool vs 原生分配，不同分辨率 (VGA~4K)
- **内存扩展性** — 1~64 通道内存占用曲线

```bash
cmake -B build -DLOONG_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/pipeline_bench
./build/memory_bench
./build/inference_bench
```

## 许可证

[Apache License 2.0](LICENSE) — Copyright 2026 Loong AI NVR Project Authors.
