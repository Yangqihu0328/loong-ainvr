# 贡献指南

欢迎为 Loong AI NVR 项目做出贡献！本文档将帮助您了解如何参与项目开发。

## 开发环境设置

### 系统要求

- **操作系统**: Ubuntu 22.04 或更高版本
- **编译器**: 支持 C++17 的编译器（GCC 7+ 或 Clang 5+）
- **构建工具**: CMake 3.10+, Ninja
- **前端工具**: Node.js 16+, npm 或 yarn

### 安装依赖

运行以下脚本安装所有必需的依赖项：

```bash
./scripts/install-deps.sh
```

该脚本会自动安装项目所需的所有系统依赖和开发工具。

## 构建项目

### 后端（C++）

使用 CMake 和 Ninja 构建项目：

```bash
cmake -B build -G Ninja
ninja -C build
```

构建产物将位于 `build/` 目录中。

### 前端（Vue.js 3）

进入前端目录并安装依赖：

```bash
cd web
npm install
```

启动开发服务器：

```bash
npm run dev
```

前端开发服务器将在 `http://localhost:5173` 启动（端口可能因配置而异）。

## 测试

### 运行测试

在构建目录中运行 CTest：

```bash
cd build
ctest
```

或者使用更详细的输出：

```bash
ctest --output-on-failure
```

### 编写测试

- 所有新功能都应包含相应的单元测试
- 测试文件应放在 `tests/` 目录中
- 使用 Google Test 框架编写测试

## 代码风格

### C++ 代码风格

本项目遵循 **Google C++ Style Guide**。

#### 代码格式化

使用 `clang-format` 格式化代码：

```bash
clang-format -i <文件路径>
```

或者格式化整个项目：

```bash
find . -name "*.cpp" -o -name "*.h" | xargs clang-format -i
```

#### 静态分析

使用 `clang-tidy` 进行静态代码分析：

```bash
clang-tidy <文件路径> -- -I<包含路径>
```

**重要**: 提交代码前，请确保：
- 代码已通过 `clang-format` 格式化
- 代码已通过 `clang-tidy` 检查，无严重警告

### 前端代码风格

- 遵循 Vue.js 3 官方风格指南
- 使用 ESLint 和 Prettier 进行代码检查和格式化
- 运行 `npm run lint` 检查代码风格

## Git 工作流程

### 分支策略

1. **主分支**: `main` 分支保持稳定，只接受通过 PR 合并的代码
2. **功能分支**: 从 `main` 创建功能分支进行开发
   ```bash
   git checkout -b feature/your-feature-name
   ```

### 提交流程

1. **创建功能分支**
   ```bash
   git checkout main
   git pull origin main
   git checkout -b feature/your-feature-name
   ```

2. **进行开发并提交**
   - 保持提交粒度合理，每个提交应是一个逻辑单元
   - 遵循提交消息规范（见下文）

3. **推送分支并创建 Pull Request**
   ```bash
   git push origin feature/your-feature-name
   ```
   然后在 GitHub/GitLab 上创建 Pull Request。

4. **等待 CI 通过**
   - 所有 CI 检查必须通过
   - 修复任何 CI 失败的问题

5. **代码审查**
   - 至少需要 1 位维护者的批准
   - 根据审查意见进行修改

6. **合并**
   - 审查通过后，维护者将合并 PR

### 提交消息格式

使用 **Conventional Commits** 格式：

```
<类型>(<范围>): <简短描述>

[可选的详细描述]

[可选的脚注]
```

**类型**包括：
- `feat`: 新功能
- `fix`: 修复 bug
- `docs`: 文档更新
- `refactor`: 代码重构
- `test`: 测试相关
- `chore`: 构建过程或辅助工具的变动

**示例**：
```
feat(camera): 添加 RTSP 流支持

实现了 RTSP 视频流的接收和转码功能，支持 H.264 编码格式。

Closes #123
```

```
fix(api): 修复用户认证 token 过期问题

修复了 token 过期后未正确刷新的问题，现在会自动刷新 token。

Fixes #456
```

## 代码审查

### 审查要求

- 所有代码变更必须通过 Pull Request 提交
- 至少需要 **1 位维护者**的批准才能合并
- 审查者会检查：
  - 代码质量和风格
  - 测试覆盖率
  - 文档完整性
  - 性能影响

### 审查指南

- 保持开放和建设性的态度
- 及时响应审查意见
- 对于有争议的问题，通过讨论达成共识

## 问题报告

### 报告 Bug

在 GitHub/GitLab Issues 中报告问题时，请包含：

1. **问题描述**: 清晰描述遇到的问题
2. **复现步骤**: 
   ```
   1. 执行操作 A
   2. 执行操作 B
   3. 观察到的错误
   ```
3. **预期行为**: 应该发生什么
4. **实际行为**: 实际发生了什么
5. **环境信息**:
   - 操作系统版本
   - 编译器版本
   - 相关依赖版本
6. **日志/错误信息**: 如果有，请附上相关日志
7. **截图**: 如果适用，请附上截图

### 功能建议

提出新功能建议时，请说明：
- 功能的使用场景
- 预期的工作方式
- 可能的实现方案（可选）

## 其他资源

- 项目文档: 查看 `docs/` 目录
- 代码示例: 参考现有代码实现
- 问题讨论: 在 Issues 中提问或讨论

## 联系方式

如有疑问，请通过以下方式联系：
- 创建 Issue 进行讨论
- 联系项目维护者

感谢您的贡献！
