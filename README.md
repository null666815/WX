# 📱 即时通信项目 - 现代化架构版本

<div align="center">
  <img src="https://cdn.acwing.com/media/user/profile/photo/489144_lg_fa02d42392.jpg" alt="即时通信Logo" width="120"/>
  
  <div style="margin: 1rem 0;">
    <img src="https://img.shields.io/badge/Language-C%2B%2B17-blue.svg" alt="C++17"/>
    <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="MIT License"/>
    <img src="https://img.shields.io/badge/Build-CMake-yellow.svg" alt="CMake Build"/>
    <img src="https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-orange.svg" alt="Multi-Platform"/>
  </div>
  
  <p>轻量级、高可扩展的即时通信系统，基于分层架构与现代C++设计模式实现</p>
</div>


## 📂 项目结构

```
WX/
├── src/                   # 🔧 核心源码目录（分层架构实现）
│   ├── core/              # 🏗️ 业务核心层 - 数据模型与核心逻辑
│   │   ├── User.hpp/cpp       # 用户实体（认证、状态管理）
│   │   ├── Group.hpp/cpp      # 群组实体（成员管理、权限控制）
│   │   ├── Message.hpp/cpp    # 消息实体（文本/离线消息封装）
│   │   └── Platform.hpp/cpp   # 平台管理（全局状态、服务注册）
│   ├── network/           # 🌐 网络传输层 - 通信抽象封装
│   │   ├── tcp_socket.hpp     # TCP套接字接口（跨平台兼容）
│   │   └── tcp_socket.cpp     # 实现（非阻塞IO、超时控制、错误处理）
│   ├── chat/              # 💬 应用层 - 聊天业务逻辑
│   │   ├── ChatServer.hpp/cpp # 服务器核心（连接管理、请求分发）
│   │   └── ClientHandler.hpp/cpp # 客户端会话（消息解析、状态维护）
│   └── common/            # 🛠️ 通用组件层 - 跨模块共享工具
│       ├── ThreadPool.hpp/cpp # 线程池（任务调度、并发控制）
│       ├── Repository.hpp/cpp # 数据持久化（文件存储、读写封装）
│       ├── Protocol.hpp/cpp   # 通信协议（命令解析、响应构造）
│       ├── Service.hpp        # 服务接口（解耦业务与实现）
│       └── WeChatService.hpp/cpp # 微信核心服务（业务逻辑实现）
├── examples/              # 📚 快速示例程序（开箱即用）
│   ├── simple_chat_client.cpp  # 单文件客户端（基础聊天功能）
│   └── simple_chat_server.cpp # 单文件服务器（极简启动示例）
├── data/                  # 💾 数据存储目录（默认文件存储）
│   ├── users.txt          # 用户数据（账号、密码、状态）
│   └── groups.txt         # 群组数据（成员列表、群组信息）
├── CMakeLists.txt         # ⚙️ 现代构建配置（跨平台兼容）
├── main_test.cpp          # 🧪 功能测试入口（核心模块验证）
└── README.md              # 📖 项目全量文档（使用/开发指南）
```


## 📦 构建与运行指南

### 🔍 依赖说明
- **编译器**: 支持C++17及以上（GCC 8+/Clang 7+/MSVC 2019+）
- **构建工具**: CMake 3.15+（推荐）或直接使用编译器
- **跨平台**: 兼容Linux/macOS/Windows


### 🚀 方式1：使用CMake构建（推荐）
```bash
# 1. 创建构建目录（避免污染源码）
mkdir -p build && cd build

# 2. 生成构建文件（自动检测环境）
cmake .. -DCMAKE_BUILD_TYPE=Release  # Release模式（优化性能）
# 或 Debug模式（用于开发调试）：cmake .. -DCMAKE_BUILD_TYPE=Debug

# 3. 编译项目（-j 后接CPU核心数，加速编译）
make -j4

# 4. 运行示例（在build目录下）
./examples/simple_chat_server &  # 后台启动服务器
./examples/simple_chat_client    # 启动客户端（可多开）
```


### 🚀 方式2：直接编译（快速验证）
#### Linux/macOS
```bash
# 编译服务器
g++ examples/simple_chat_server.cpp src/network/tcp_socket.cpp src/common/*.cpp \
  -o server -std=c++17 -O2 -lpthread

# 编译客户端
g++ examples/simple_chat_client.cpp src/network/tcp_socket.cpp src/common/*.cpp \
  -o client -std=c++17 -O2 -lpthread

# 运行
./server &
./client
```

#### Windows（PowerShell/CMD）
```bash
# 使用MSVC编译器（需配置VS环境变量）
cl /EHsc /MT /std:c++17 examples\simple_chat_server.cpp src\network\tcp_socket.cpp src\common\*.cpp /Fe:server.exe
cl /EHsc /MT /std:c++17 examples\simple_chat_client.cpp src\network\tcp_socket.cpp src\common\*.cpp /Fe:client.exe

# 运行
start server.exe
client.exe
```


## ✨ 核心功能特性

| 功能模块         | 具体特性                                  | 实现状态 | 核心依赖模块               |
|------------------|-------------------------------------------|----------|----------------------------|
| 🔌 网络通信      | 跨平台TCP通信、非阻塞IO、超时重连          | ✅ 已完成 | network/tcp_socket         |
| 👥 用户管理      | 账号注册、登录认证、在线状态同步            | ✅ 已完成 | core/User、common/Repository |
| 🗣️ 聊天功能      | 单聊/群聊、实时消息、消息回执              | ✅ 已完成 | chat/ChatServer、core/Message |
| 📥 离线消息      | 离线消息缓存、上线后自动拉取                | ✅ 已完成 | core/Message、common/Repository |
| ⚡ 并发处理      | 多线程客户端管理、任务池调度                | ✅ 已完成 | common/ThreadPool          |
| 📜 协议解析      | 自定义命令协议、请求/响应统一封装          | ✅ 已完成 | common/Protocol            |
| 🔍 状态监测      | 客户端连接状态、异常断开处理                | ✅ 已完成 | chat/ClientHandler         |


## 🏗️ 架构设计优势

### 1. 分层架构（解耦与可扩展）
| 架构层级         | 核心职责                                  | 核心组件                          | 设计目标                  |
|------------------|-------------------------------------------|-----------------------------------|---------------------------|
| **业务核心层**   | 定义数据模型与核心业务规则                | User/Group/Message/Platform       | 稳定业务逻辑，隔离变化    |
| **网络传输层**   | 抽象通信能力，屏蔽跨平台差异              | TcpSocket                         | 通信与业务解耦，便于替换  |
| **应用层**       | 封装具体业务流程，衔接核心与网络层        | ChatServer/ClientHandler          | 聚焦业务实现，易于扩展    |
| **通用组件层**   | 提供跨模块工具能力，复用代码              | ThreadPool/Repository/Protocol    | 减少重复开发，统一规范    |


### 2. 现代设计模式应用
| 设计模式         | 应用场景                                  | 实现效果                          |
|------------------|-------------------------------------------|-----------------------------------|
| **命令模式**     | 协议命令解析（如消息发送、用户登录）      | 新增命令无需修改核心逻辑，只需添加处理器 |
| **生产者-消费者** | 异步消息队列（离线消息存储与拉取）        | 解耦消息生产与消费，平衡系统负载        |
| **模板方法**     | 协议处理流程（请求验证→解析→响应）        | 统一流程规范，自定义步骤只需重写方法    |
| **工厂模式**     | 服务实例化（如WeChatService创建）         | 隐藏实例化细节，便于服务替换与测试      |


## 🛠️ 开发指南

### 📝 新增功能步骤
1. **定位层级**: 确定功能归属（如“文件传输”属于应用层，需在`chat/`下开发）
2. **创建文件**: 在对应目录添加`.hpp`（接口）和`.cpp`（实现），遵循现有命名规范
3. **集成构建**: 更新`CMakeLists.txt`，将新文件添加到对应目标（如`chat`模块）
4. **测试验证**: 在`main_test.cpp`中添加测试用例，或扩展`examples/`示例程序
5. **文档更新**: 补充功能说明到README或对应模块注释


### 📜 扩展通信协议示例
在`common/Protocol.hpp`中添加自定义命令，无需修改核心逻辑：
```cpp
// 1. 注册新协议处理器（命令：SEND_FILE，处理文件发送请求）
Protocol::getInstance().addHandler(
    "SEND_FILE",  // 协议命令（客户端与服务器需一致）
    [](const std::string& data, ClientHandler* handler) -> std::string {
        // 解析客户端发送的文件信息（如文件名、大小、数据）
        auto fileInfo = Protocol::parseData(data);
        std::string fileName = fileInfo["fileName"];
        std::string fileData = fileInfo["fileData"];

        // 业务逻辑：保存文件/转发给目标用户
        auto userService = ServiceFactory::getWeChatService();
        bool success = userService->sendFile(handler->getCurrentUser(), fileInfo);

        // 构造响应
        return Protocol::buildResponse(success ? "OK" : "FAIL", 
                                      success ? "文件发送成功" : "文件发送失败");
    }
);

// 2. 客户端发送协议请求
std::string request = Protocol::buildRequest("SEND_FILE", {
    {"fileName", "test.txt"},
    {"fileData", "base64编码的文件内容"}
});
tcpSocket.sendPipeMessage(request);
```


## 📋 开发 roadmap

### 🔴 高优先级（核心优化）
- [ ] 集成单元测试框架（如Google Test），覆盖核心模块
- [ ] 配置外部化（支持XML/YAML/JSON配置，替换硬编码）
- [ ] 添加结构化日志系统（如spdlog），支持日志分级与滚动


### 🟡 中优先级（功能增强）
- [ ] 替换文件存储为数据库（如SQLite/MySQL），支持事务与索引
- [ ] 新增消息类型（图片/语音/表情），扩展协议支持
- [ ] 实现用户头像与个人资料管理


### 🟢 低优先级（体验与扩展）
- [ ] 添加UI界面（如基于Qt/SDL，支持图形化操作）
- [ ] 集成加密机制（如TLS通信加密、消息内容加密）
- [ ] 支持跨设备登录（多端在线，消息同步）



<div align="center">
  <p>💡 如有问题或建议，欢迎提交Issue或联系开发者！</p>

</div>
