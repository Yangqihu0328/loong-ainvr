# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.3.0] - 2026-02-19

### Added
- **智能分析规则引擎**: 通用规则框架 (Rule → Evaluator → RuleStage)，支持越线检测、区域入侵、目标计数、徘徊检测四种分析规则
- **IOU Tracker**: 基于 IOU 贪心匹配的帧间目标追踪器，支持轨迹管理和生命周期维护
- **多模型级联推理**: ModelCascade 编排器，支持 Primary/Crop/Parallel 三种级联模式
- **车牌识别 (LPR)**: 两阶段识别 — LprDetectorAdapter (YOLO 车牌检测) + LprOcrAdapter (CRNN 文字识别，68 字符中国车牌字符集)
- **人脸检测与属性分析**: FaceDetectorAdapter (SCRFD/RetinaFace) + FaceAttributeAdapter (ArcFace 512-d embedding + 年龄/性别)
- **车牌/人脸数据存储**: PlateStore + FaceStore (SQLite)，支持模糊搜索、时间范围查询、embedding 余弦相似度检索
- **WebRTC 超低延迟直播**: 基于 loong-rtc 的 WebRTC 信令与媒体服务，延迟 <200ms，支持 H.264/H.265/VP8 + DTLS-SRTP
- **WebRTC 前端播放器**: NvrPlayer 新增 WebRTC 模式，自动协议降级 (WebRTC → FLV → HLS)
- **数据分析聚合服务**: AnalyticsAggregator — 实时事件聚合、趋势统计 (小时/天/周/月)、热力图、高峰时段分析
- **分析仪表盘前端**: ECharts 可视化 — 趋势折线图、类型分布饼图、24h 柱状图、Canvas 热力图
- **响应式布局改造**: 全部页面支持手机/平板访问，移动端 drawer 侧边栏、触摸手势、自适应网格
- **GIS 地图视图**: Leaflet.js 集成，摄像头 GPS 标注、状态着色、告警闪烁、点击预览
- **PWA 支持**: manifest.json + Service Worker 离线缓存 + Web Push 推送通知 + 安装提示
- **AI 模型插件系统**: dlopen 动态加载第三方 .so 模型插件，插件目录扫描 + API 版本兼容检查
- **MQTT 集成**: MqttChannel 通知渠道，支持 paho-mqtt / stub 模式，告警事件自动发布 + 命令订阅
- **云存储备份**: S3 兼容云存储 (AWS S3 / MinIO / 阿里云 OSS)，后台定期备份 + 策略配置
- **告警管理系统**: AlarmManager 告警规则 CRUD + 告警查询 + 确认，7 个 REST API 端点
- **规则存储**: SQLite rules + rule_events 表，规则 CRUD + 事件日志 + 冷却时间
- API 端点: 新增 30+ 个端点 (Rules/Analytics/Plates/Faces/WebRTC/Plugins/MQTT/Backup/Alarms)

### Changed
- Pipeline 扩展为 6 阶段: Input → Decode → AI → **Rule** → Overlay → Output
- AiStage 支持单引擎和 ModelCascade 级联两种推理模式
- OutputStage 新增 WebRTC 视频帧回调
- OsdRenderer 增强: 虚拟线/多边形区域/计数数字/徘徊高亮绘制
- ChannelOrchestrator 管线构建集成 RuleStage + ModelCascade
- main.cc 子系统初始化扩展到 21 个组件，优雅关闭按正确顺序执行
- 通道配置新增 GPS 坐标 (latitude/longitude)、codec/bitrate/analysis_fps 等字段
- API 端点总数从 37 增至 65+
- 单元测试从 120+ 增至 250+
- 前端新增 ECharts、Leaflet.js、hls.js 依赖

### Fixed
- SignalHandler 安全: 移除非 async-signal-safe 的 spdlog 调用，改用 sigaction
- AlarmManager bad_any_cast: 统一为 JSON string 事件格式
- 并发 UAF: Pipeline 改 shared_ptr + 锁内拷贝策略
- EventBus 清理: shutdown 时 Unsubscribe 防止 UAF
- PluginManager 卸载: UnloadAll() 确保 dlclose
- 时间戳字段统一 (timestamp_ms → timestamp)
- auto_recovery 原子操作防竞争
- MQTT 字段名对齐 (broker→broker_host, port→broker_port)
- 15 个 v0.3.0 新增端点补全 CheckAuth 认证
- ctx.role 编译错误修复 (改为 ctx.claims.role 枚举比较)
- HandleGetChannel/ListChannels 配置字段补全
- HandleCreateChannel 输入验证 (name/rtsp_url 非空)
- 全部空 catch(...) {} 改为 catch(std::exception&) + 日志记录

### Security
- 全部 v0.3.0 新增 API 端点强制认证
- WebRTC 会话通道注销 (通道停止时关闭相关会话)
- Lambda 捕获安全 (hot-reload lambdas 改值捕获 shared_ptr)

## [0.2.0] - 2026-02-19

### Added
- CI/CD pipeline with GitHub Actions (build matrix: GCC 11/12 + Clang 14, Debug/Release)
- Docker containerization (multi-stage Dockerfile + docker-compose + GPU variant)
- HTTPS/TLS support with auto-detection and self-signed certificate generation
- Security hardening: forced password change, login rate limiting (5 attempts/15min lockout), API rate limiting (120 req/60s), security headers
- clang-tidy static analysis with CI integration (808→11 warnings)
- Code coverage reporting (gcov + lcov → Codecov)
- Health check endpoint (GET /api/health)
- Prometheus metrics endpoint (GET /metrics)
- Structured JSON logging option
- HLS streaming support (TsMuxer + HlsService + hls.js frontend)
- Alarm notification system (SMTP email + Webhook with HMAC-SHA256)
- Enhanced recording playback (timeline visualization, speed control 0.25x-8x, AI event search)
- Performance benchmarks (Pipeline throughput, Memory pool, AI inference)
- Configuration hot-reload (inotify file watcher + API + WebSocket broadcast)
- Developer documentation (CONTRIBUTING.md, CHANGELOG.md, LICENSE)

### Changed
- API endpoints increased from 22 to 37
- Unit tests increased from 88 to 120+
- Frontend dependencies: added hls.js

## [0.1.0] - 2026-02-17

### Added
- Core framework: ThreadPool, EventBus, FrameBufferPool, ConfigManager, Logger
- 5-stage video pipeline: Input → Decode → AI → Overlay → Output
- FFmpeg H.264/H.265 decoder and encoder with hardware acceleration detection
- AI engine: OpenCV DNN + ONNX Runtime + TensorRT backends, YOLO v5/v7/v8/v9/v10/v11
- 64-channel concurrent management with auto-recovery
- Storage: MP4 segmented recording + SQLite WAL index + circular cleanup
- Network: REST API (22 endpoints), HTTP-FLV streaming, WebSocket push, RTSP server
- Web UI: Vue.js 3 + Element Plus (Dashboard, Channels, LiveView, Recordings, Events, Users, Settings)
- Authentication: JWT + RBAC (admin/operator/viewer)
- ONVIF device discovery and PTZ control
