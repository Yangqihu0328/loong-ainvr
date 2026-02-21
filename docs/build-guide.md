# 构建与部署指南

## 系统要求

### 开发环境
- **操作系统**: Ubuntu 22.04 LTS (x86_64) 或 WSL2
- **编译器**: GCC 11+ 或 Clang 14+
- **CMake**: 3.20+
- **Node.js**: 20+ (Web UI 开发)

### 目标平台
- 嵌入式 Linux (ARM64 / x86_64)
- 最低内存: 2GB (推荐 8GB+)
- 磁盘: 取决于录像存储需求

---

## 安装依赖

### 一键安装 (Ubuntu)

```bash
bash scripts/install-deps.sh
```

### 手动安装

```bash
# 构建工具
sudo apt install -y build-essential cmake ninja-build pkg-config

# FFmpeg
sudo apt install -y libavcodec-dev libavformat-dev libavutil-dev \
    libswscale-dev libswresample-dev

# OpenCV
sudo apt install -y libopencv-dev

# C++ 库
sudo apt install -y libspdlog-dev nlohmann-json3-dev libsqlite3-dev \
    libssl-dev

# v0.3.0 新增依赖 (WebRTC / MQTT)
sudo apt install -y libsrtp2-dev libwebsockets-dev libpaho-mqtt-dev

# 测试框架
sudo apt install -y libgtest-dev libgmock-dev

# Node.js (Web UI)
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs
```

---

## 构建后端

### Release 构建 (推荐)

```bash
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja
```

### Debug 构建

```bash
mkdir build-debug && cd build-debug
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ..
ninja
```

### 使用 Makefile (推荐)

```bash
make build           # Debug 构建
make release         # Release 构建
```

### 构建选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `LOONG_BUILD_TESTS` | `ON` | 编译单元测试 |
| `LOONG_BUILD_BENCHMARKS` | `OFF` | 编译性能测试 |
| `LOONG_ENABLE_ONNXRUNTIME` | `OFF` | 启用 ONNX Runtime 后端 |
| `LOONG_ENABLE_TENSORRT` | `OFF` | 启用 TensorRT 后端 |
| `LOONG_ENABLE_SANITIZERS` | `OFF` | 启用 Address/UB Sanitizer |
| `LOONG_ENABLE_CLANG_TIDY` | `OFF` | 启用 clang-tidy 静态分析 |
| `LOONG_ENABLE_COVERAGE` | `OFF` | 启用代码覆盖率 |

```bash
# 例: 禁用测试 + 启用 Sanitizer
cmake -G Ninja \
    -DLOONG_BUILD_TESTS=OFF \
    -DLOONG_ENABLE_SANITIZERS=ON \
    ..
```

### 构建产物

```
build/
├── loong-ainvr              # 主可执行文件
├── core_tests               # Core 单元测试
├── channel_tests            # Channel 单元测试
├── ai_engine_tests          # AI Engine 单元测试
├── storage_tests            # Storage 单元测试
├── system_tests             # Auth 单元测试
├── network_tests            # Network 单元测试
├── rules_tests              # Rules 单元测试
├── analytics_tests          # Analytics 单元测试
├── video_input_tests        # ONVIF 单元测试
├── overlay_tests            # Overlay 单元测试
├── codec_tests              # Codec 单元测试
└── integration_tests        # 集成测试
```

---

## 运行测试

```bash
cd build

# 运行全部测试
ctest --output-on-failure

# 使用 Makefile
make test

# 运行单个测试集
./system_tests           # Auth 模块测试
./core_tests             # Core 模块测试
./rules_tests            # 规则引擎测试
./ai_engine_tests        # AI 引擎测试 (含 LPR/Face)
./analytics_tests        # 分析聚合测试

# 运行特定测试用例
./system_tests --gtest_filter="UserStoreTest.*"
./rules_tests --gtest_filter="CrossLineEvaluatorTest.*"
```

### 测试概览 (250+ 用例)

| 测试集 | 用例数 | 覆盖范围 |
|--------|--------|---------|
| `core_tests` | 19 | 线程池、事件总线、内存池、配置、队列、管线 |
| `channel_tests` | 5 | 通道创建/删除/启停、上限 |
| `ai_engine_tests` | 65 | YOLO、ModelCascade、LPR (19)、Face (33) |
| `storage_tests` | 9 | SQLite CRUD、配额、清理、时间轴 |
| `system_tests` | 24 | UserStore、JWT、AuthMiddleware RBAC |
| `network_tests` | 13 | HTTP API 路由、认证 |
| `rules_tests` | 75 | RuleStore (8)、IouTracker (10)、CrossLine (9)、Region (13)、Counting (10)、Loitering (11)、RuleEngine (6)、RuleStage (4)、RuleTypes (2)、Evaluator (2) |
| `analytics_tests` | 21 | AnalyticsStore (11)、Aggregator (9)、TimeGranularity |
| `overlay_tests` | 18 | OsdRenderer (5)、OverlayStage (5)、Compositor (8) |
| `codec_tests` | 22 | FFmpeg Encoder/Decoder、CodecFactory |
| `integration_tests` | 9 | 端到端集成验证 |

---

## 构建 Web UI

### 开发模式

```bash
cd web
npm install
npm run dev
```

开发服务器启动在 `http://localhost:3000`，自动代理 API 请求到后端 `http://localhost:8080`。

### 生产构建

```bash
cd web
npm run build
```

构建产物输出到 `web/dist/`，可嵌入到 C++ HTTP Server 或使用 Nginx 部署。

---

## 本地运行环境 (build/bin/)

### 一键打包 + 运行

最简单的方式，一条命令完成编译、打包、启动：

```bash
make run              # Debug 模式
make run-release      # Release 模式
```

### 仅打包 (不启动)

```bash
make package          # Debug 模式打包到 build/bin/
make package-release  # Release 模式打包到 build/bin/
```

### 手动打包

```bash
# 1. 构建后端
make build    # 或 make release

# 2. 构建前端
make web

# 3. 组装运行时环境
bash scripts/setup-runtime.sh debug    # 或 release
```

### 运行时目录结构

```
build/bin/
├── loong-ainvr              # 可执行文件
├── config.json              # 本地配置 (相对路径, 日志级别 debug)
├── web/                     # Web UI 静态资源
├── models/                  # YOLO/LPR/Face 模型 (.onnx)
├── plugins/                 # AI 模型插件 (.so)
├── recordings/              # 录像存储目录
└── data/                    # SQLite 数据库 (自动创建)
    ├── users.db             # 用户认证数据
    ├── index.db             # 录像索引
    └── rules.db             # 分析规则数据
```

### 手动启动

```bash
cd build/bin
./loong-ainvr config.json
```

### 配置说明

`config/local.json` 是本地开发专用配置，与 `config/default.json` 的区别：

| 配置项 | default.json (生产) | local.json (本地) |
|--------|-------------------|------------------|
| `system.log_level` | `info` | `debug` |
| `storage.base_path` | `/recordings` | `./recordings` |
| `storage.index_db` | `/recordings/index.db` | `./data/index.db` |
| `auth.db_path` | (默认) | `./data/users.db` |
| `rules.db_path` | `/recordings/rules.db` | `./data/rules.db` |

所有路径均为相对路径，工作目录为 `build/bin/`。

### 启动输出

```
====================================
  Loong AI NVR v0.3.0
  Embedded Video Analysis System
  Max Channels: 64
====================================
EventBus initialized
ThreadPool initialized (8 workers)
RecordIndex: opened './data/index.db'
StorageCleaner: started
UserStore: opened './data/users.db'
UserStore: created default admin account (user: admin, pass: admin123)
RuleStore: opened './data/rules.db'
RuleEngine: initialized
AnalyticsAggregator: started
AlarmManager: initialized
NotificationManager: initialized
PluginManager: scanning ./plugins/
ChannelManager: initialized (max 64 channels)
HTTP API server: listening on 0.0.0.0:8080
HTTP-FLV service: listening on 0.0.0.0:8081

======== System Ready ========
  HTTP API:  http://0.0.0.0:8080/api/
  Health:    http://0.0.0.0:8080/api/health
  Metrics:   http://0.0.0.0:8080/metrics
  FLV Live:  http://0.0.0.0:8081/live/
  Web UI:    http://0.0.0.0:3000/
  Auth:      JWT (default admin/admin123)
==============================
```

### 停止服务

按 `Ctrl+C` 发送 SIGINT 信号，系统将优雅关闭全部 21 个子系统。

---

## 交叉编译 (ARM64)

```bash
mkdir build-arm64 && cd build-arm64
cmake -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-aarch64.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    ..
ninja
```

> 需要预先准备 ARM64 交叉编译工具链和对应的依赖库。

---

## 部署

### 最小部署文件

```
/opt/loong-ainvr/
├── loong-ainvr              # 可执行文件
├── config.json              # 配置文件
├── web/                     # Web UI 静态资源
├── models/                  # AI 模型文件 (.onnx)
├── plugins/                 # AI 模型插件 (.so)
├── recordings/              # 录像存储目录
└── data/                    # 数据库目录
```

### 使用 setup-runtime.sh 打包

```bash
# 打包到 build/bin/
bash scripts/setup-runtime.sh release

# 复制到目标机器
scp -r build/bin/ user@target:/opt/loong-ainvr/

# 在目标机器启动
ssh user@target "cd /opt/loong-ainvr && ./loong-ainvr config.json"
```

### Docker 部署

```bash
# 构建镜像
make docker

# 启动 (CPU)
docker compose up -d

# 启动 (NVIDIA GPU)
docker compose -f docker-compose.yml -f docker-compose.gpu.yml up -d
```

### Systemd 服务

创建 `/etc/systemd/system/loong-ainvr.service`：

```ini
[Unit]
Description=Loong AI NVR
After=network.target

[Service]
Type=simple
ExecStart=/opt/loong-ainvr/loong-ainvr /opt/loong-ainvr/config.json
WorkingDirectory=/opt/loong-ainvr
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable loong-ainvr
sudo systemctl start loong-ainvr
sudo journalctl -u loong-ainvr -f
```

---

## 代码格式化

```bash
# 格式化所有 C++ 源文件
make format

# 检查格式 (CI 用)
make format-check
```

项目使用 `.clang-format` 配置 (Google Style)。

---

## 常用 Make 命令速查

| 命令 | 说明 |
|------|------|
| `make build` | Debug 构建 |
| `make release` | Release 构建 |
| `make test` | 运行全部测试 |
| `make package` | 构建 + 打包到 build/bin/ (Debug) |
| `make package-release` | 构建 + 打包到 build/bin/ (Release) |
| `make run` | 构建 + 打包 + 启动服务 (Debug) |
| `make run-release` | 构建 + 打包 + 启动服务 (Release) |
| `make web` | 构建前端 |
| `make format` | 格式化 C++ 代码 |
| `make lint` | clang-tidy 静态分析 |
| `make coverage` | 代码覆盖率报告 |
| `make docker` | 构建 Docker 镜像 |
| `make clean` | 清理构建目录 |
