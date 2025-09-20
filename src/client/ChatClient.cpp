#include "ChatClient.hpp"
#include <random>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <conio.h>
#else
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>

// Cross-platform keyboard input functions
inline int _kbhit() {
    struct termios oldt, newt;
    int ch;
    int oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if (ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }

    return 0;
}

inline int _getch() {
    struct termios oldt, newt;
    int ch;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    return ch;
}
#endif

// Constructor and Destructor
ChatClientApp::ChatClientApp() : m_threadPool() {}

ChatClientApp::~ChatClientApp() {
    disconnect();
    if (m_wxService) delete m_wxService;
}

// Platform and User Management
void ChatClientApp::setupPlatform() {
    m_platform.users.emplace("alice", User("alice", "Alice"));
    m_platform.users.emplace("bob", User("bob", "Bob"));
    m_platform.groups.emplace("group1", Group("group1", GroupType::QQ));
    m_platform.groups.emplace("wxgroup1", Group("wxgroup1", GroupType::WeChat));
    std::cout << "平台已初始化，包含示例用户和群组" << std::endl;
}

void ChatClientApp::setUser(const std::string &userId) {
    m_userId = userId;
    if (m_wxService) delete m_wxService;
    m_wxService = new WeChatService();
    m_wxService->attachPlatform(&m_platform);
    std::cout << "用户已设置为：" << userId << std::endl;
}

// Connection Management
bool ChatClientApp::connect(const std::string &serverIp, uint16_t serverPort) {
    if (m_connected) disconnect();

    if (!m_socket.init()) {
        std::cerr << "初始化客户端套接字失败" << std::endl;
        return false;
    }

    if (!m_socket.connect(serverIp, serverPort)) {
        std::cerr << "连接到服务器失败 " << serverIp << ":" << serverPort << std::endl;
        return false;
    }

    m_connected = true;
    std::cout << "成功连接到服务器 " << serverIp << ":" << serverPort << std::endl;

    m_threadPool.stop();

    if (!m_userId.empty()) {
        std::string loginMsg = "LOGIN|" + m_userId;
        std::cout << "[Client] Preparing to send LOGIN message: '" << loginMsg << "'" << std::endl;

        if (m_socket.sendPipeMessage(loginMsg)) {
            std::cout << "用户登录消息已发送：" << m_userId << std::endl;

            // 优化后的等待登录确认 - 保持简洁，让异步队列处理后续消息
            std::cout << "等待登录确认..." << std::endl;
            std::string initialResponse;
            bool loginConfirmed = false;

            // 延长等待时间以确保接收到捎带消息(如果有的话)
            for (int attempt = 0; attempt < Config::LOGIN_TIMEOUT_SECONDS * 2; ++attempt) {
                if (m_socket.receivePipeMessage(initialResponse, 1)) {
                    std::cout << "[Client] 收到登录响应: " << initialResponse << std::endl;

                    if (initialResponse.find("LOGIN_OK") != std::string::npos) {
                        std::cout << "✅ 登录确认完成！" << std::endl;

                        // 检查是否包含捎带离线消息
                        if (initialResponse.find("OFFLINE_COUNT:") != std::string::npos) {
                            std::cout << "[Client] 检测到捎带离线消息，在异步队列中处理" << std::endl;
                            displayOfflineMessages(initialResponse);
                        } else {
                            std::cout << "[Client] 无离线消息捎带" << std::endl;
                        }

                        loginConfirmed = true;
                        break;
                    }
                } else {
                    std::cout << "[Client] 等待登录确认... (尝试 " << (attempt + 1) << "/" << (Config::LOGIN_TIMEOUT_SECONDS * 2) << ")" << std::endl;
                }
            }

            if (!loginConfirmed) {
                std::cerr << "登录确认超时！" << std::endl;
                disconnect();
                return false;
            }

            // Start async message queue system
            std::cout << "[Client] 登录成功，开始启动异步消息队列系统..." << std::endl;
            m_messagesReceived = 0;
            m_messagesProcessed = 0;

            // Start producer thread
            m_listenerThread = std::thread(&ChatClientApp::messageProducer, this);

            #ifdef _WIN32
            Sleep(200);  // 增加延迟确保消息处理完成
            #else
            usleep(200 * 1000);
            #endif

            // Start consumer thread
            m_messageProcessorThread = std::thread(&ChatClientApp::messageConsumer, this);

        } else {
            std::cerr << "登录消息发送失败: " << m_socket.getLastError() << std::endl;
            return false;
        }
    }
    return true;
}

void ChatClientApp::disconnect() {
    if (m_connected) {
        m_connected = false;
        m_messageQueue.finish();

        if (m_listenerThread.joinable()) {
            std::cout << "[Client] 等待生产者线程结束..." << std::endl;
            m_listenerThread.join();
        }

        if (m_messageProcessorThread.joinable()) {
            std::cout << "[Client] 等待消费者线程结束..." << std::endl;
            m_messageProcessorThread.join();
        }

        m_socket.close();
        m_socket.cleanup();

        // Reset ThreadPool for next use
        std::cout << "[Client] 正在重置ThreadPool..." << std::endl;

        std::cout << "\n📊 通信统计:" << std::endl;
        std::cout << "   🔹 消息接收: " << m_messagesReceived << std::endl;
        std::cout << "   🔹 消息处理: " << m_messagesProcessed << std::endl;
        std::cout << "已断开与服务器的连接" << std::endl;
    }
}

// Message Processing Core Functions
void ChatClientApp::processMessageToQueue(const std::string &message) {
    std::cout << "[Client] 处理接收到的消息: " << message << std::endl;

    // 检查是否是MESSAGE类型的消息
    if (message.substr(0, 7) == "MESSAGE") {
        MessageData msgData;
        if (ProtocolProcessor::deserializeMessage(message, msgData)) {
            // 如果是发送给当前用户的消息，发送ACK确认
            if (msgData.receiverId == m_userId && !msgData.senderId.empty()) {
                std::cout << "[Client] 检测到杀消息，准备发送ACK确认..." << std::endl;
                if (sendAckMessage(msgData.messageId)) {
                    std::cout << "[Client] ✅ ACK发送成功: " << msgData.messageId << std::endl;
                } else {
                    std::cout << "[Client] ❌ ACK发送失败: " << msgData.messageId << std::endl;
                    // 继续处理消息，即使ACK失败
                }
            } else {
                std::cout << "[Client] 消息不是发送给自己的，跳过ACK" << std::endl;
            }

            // 将消息添加到队列进行显示，不管ACK是否发送成功
            MessageData msgForQueue(msgData.senderId, msgData.receiverId, msgData.content);
            if (!msgData.timestamp.empty()) {
                msgForQueue.timestamp = msgData.timestamp;
            }
            std::cout << "[Client] 消息内容添加到显示队列: 发送者=" << msgData.senderId << ", 内容=" << msgData.content << std::endl;
            pushMessageToQueue(msgForQueue, true);
            return;
        } else {
            std::cout << "[Client] 解析MESSAGE失败: " << message << std::endl;
            return;
        }
    }

    // 检查是否是ACK类型的消息(客户端自己发送的ACK回显，不需要处理)
    if (message.substr(0, 3) == "ACK") {
        std::cout << "[Client] 收到ACK消息(这是自己发送的回显): " << message << std::endl;
        return; // 忽略ACK消息
    }

    // 处理其他类型的消息
    if (message.find("OFFLINE_COUNT:") != std::string::npos) {
        // 这是包含离线消息的登录响应，直接处理多个离线消息
        std::cout << "[Client] 🔥 发现离线消息响应，开始处理多个离线消息..." << std::endl;

        // 解析OFFLINE_COUNT以获取消息数量
        size_t countPos = message.find("OFFLINE_COUNT:");
        size_t countEndPos = message.find("|", countPos);
        int offlineCount = 1; // 默认至少有一个消息

        if (countEndPos != std::string::npos && countPos != std::string::npos) {
            std::string countStr = message.substr(countPos + 14, countEndPos - (countPos + 14));
            try {
                offlineCount = std::stoi(countStr);
                std::cout << "[Client] 解析得到离线消息数量: " << offlineCount << std::endl;
            } catch (...) {
                std::cout << "[Client] 无法解析离线消息数量，使用默认值1" << std::endl;
            }
        }

        // 从OFFLINE_COUNT后面开始查找所有MESSAGE段
        size_t currentPos = message.find("MESSAGE|", countEndPos);
        int processedCount = 0;

        std::cout << "[Client] 开始处理多个离线消息，预计处理 " << offlineCount << " 条消息" << std::endl;

        while (currentPos != std::string::npos && processedCount < offlineCount) {
            // 查找下一个MESSAGE开始作为当前消息结束的分隔符
            size_t nextMessagePos = message.find("MESSAGE|", currentPos + 1);
            std::string messagePart;

            if (nextMessagePos != std::string::npos) {
                messagePart = message.substr(currentPos, nextMessagePos - currentPos);
            } else {
                messagePart = message.substr(currentPos);
            }

            if (!messagePart.empty()) {
                std::cout << "[Client] 处理离线消息 #" << (processedCount + 1) << ": " << messagePart << std::endl;

                // 解析这个MESSAGE消息
                MessageData msgData;
                if (ProtocolProcessor::deserializeMessage(messagePart, msgData)) {
                    processedCount++;

                    std::cout << "[Client] 🔔 收到离线消息 #" << processedCount << " !" << std::endl;
                    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
                    std::cout << "📨 离线消息 #" << processedCount << std::endl;
                    std::cout << "👤 来自: " << msgData.senderId << std::endl;
                    std::cout << "💬 内容: " << msgData.content << std::endl;
                    if (!msgData.timestamp.empty()) {
                        std::cout << "🕐 时间: " << msgData.timestamp << std::endl;
                    }
                    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

                    // 发送ACK确认
                    if (!msgData.messageId.empty()) {
                        sendAckMessage(msgData.messageId);
                        std::cout << "[Client] ✅ 已发送离线消息 #" << processedCount << " ACK确认" << std::endl;
                    }
                } else {
                    std::cout << "[Client] ❌ 离线消息 #" << (processedCount + 1) << " 解析失败" << std::endl;
                }
            }

            // 移动到下一个MESSAGE开始位置
            currentPos = nextMessagePos;
        }

        std::cout << "[Client] ✅ 离线消息处理完成，共处理了 " << processedCount << " 条离线消息" << std::endl;

        if (processedCount > 0) {
            std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
            std::cout << "🎉 所有离线消息已读取完成！您现在可以正常收发消息了。" << std::endl;
            std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
        }

        return;
    }

    ParsedMessage parsed = parseMessage(message);
    if (!parsed.isValid()) {
        std::cout << "[Client] 消息解析失败: " << message << std::endl;
        return;
    }

    std::cout << "[Client] 处理其他类型消息: " << parsed.type << std::endl;

    if (parsed.isResponse()) {
        MessageData responseMsg("SERVER", m_userId, message);
        pushMessageToQueue(responseMsg, true);
    } else if (parsed.isSystemMessage() && message.find("OFFLINE_MESSAGES") != std::string::npos) {
        MessageData offlineMsg("SYSTEM", m_userId, message);
        pushMessageToQueue(offlineMsg, true);
    }
}

void ChatClientApp::messageProducer() {
    std::cout << "[Client] 🎯 生产者线程启动 - 专注网络I/O" << std::endl;
    while (m_connected && m_running) {
        std::string message;
        bool hasMessage = false;
        std::lock_guard<std::mutex> lock(m_socketMutex);
        if (m_connected) {
            hasMessage = m_socket.receivePipeMessage(message, 0);
        }

        if (hasMessage && !message.empty()) {
            processMessageToQueue(message);
        } else if (!hasMessage) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    m_messageQueue.finish();
    std::cout << "[Client] 🎯 生产者线程结束 - 总接收: " << m_messagesReceived << " 消息" << std::endl;
}

void ChatClientApp::messageConsumer() {
    std::cout << "[Client] 📋 消费者线程启动 - 专注消息处理" << std::endl;
    while (m_connected && m_running && !m_messageQueue.isFinished()) {
        MessageData msg;
        if (m_messageQueue.pop(msg, 100)) {
            if (msg.senderId == "SERVER") {
                if (msg.content.find("MESSAGE_SENT") != std::string::npos) {
                    std::cout << "\n✅ 消息已发送成功" << std::endl;
                } else if (msg.content.find("MESSAGE_CACHED") != std::string::npos) {
                    std::cout << "\n📨 接收方不在线，已缓存消息" << std::endl;
                } else if (msg.content.find("SEND_FAILED") != std::string::npos) {
                    std::cout << "\n⚠️ 消息发送失败" << std::endl;
                }
            } else if (msg.senderId == "SYSTEM" && msg.content.find("OFFLINE_MESSAGES") != std::string::npos) {
                std::stringstream ss(msg.content);
                std::string prefix, type, count;
                std::getline(ss, prefix, '|');
                std::getline(ss, type, '|');
                std::getline(ss, count, '|');
                std::cout << "\n📨 系统通知：收到 " << count << " 条离线消息" << std::endl;
            } else if (msg.receiverId == m_userId) {
                std::cout << "\n━━━━━━━━━━━━━━━━━━ 🔔 新消息 🔔 ━━━━━━━━━━━━━━━━━━" << std::endl;
                std::cout << "👤 来自: " << msg.senderId << std::endl;
                std::cout << "💬 消息内容: " << msg.content << std::endl;
                if (!msg.timestamp.empty()) {
                    std::cout << "🕐 时间戳: " << msg.timestamp << std::endl;
                }
                std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
            }
            m_messagesReceived++;
            std::cout << "主菜单> " << std::flush;
        }
    }
    std::cout << "[Client] 📋 消费者线程结束 - 总处理: " << m_messagesReceived << " 消息" << std::endl;
}

void ChatClientApp::sleepAndCheckConnection() {
#ifdef _WIN32
    Sleep(Config::MENU_CHECK_INTERVAL_MS);
#else
    usleep(Config::MENU_CHECK_INTERVAL_MS * 1000);
#endif

    static int connectionCheckCounter = 0;
    if (++connectionCheckCounter >= Config::CONNECTION_CHECK_THRESHOLD && !m_connected) {
        std::cout << "\n连接已断开，正在退出..." << std::endl;
        m_running = false;
    }
}

// 显示离线消息的专用函数
void ChatClientApp::displayOfflineMessages(const std::string &bundledResponse) {
    auto offlineMessages = extractOfflineMessageDetails(bundledResponse);

    if (!offlineMessages.empty()) {
        // 批量发送ACK确认
        for (const auto& msgData : offlineMessages) {
            sendAckMessage(msgData.messageId); // 静默发送ACK
        }

        std::cout << "\n📨 发现 " << offlineMessages.size() << " 条离线消息，正在为您展示...\n" << std::endl;

        // 逐条显示离线消息，使用和普通消息一样的格式
        for (size_t i = 0; i < offlineMessages.size(); ++i) {
            const auto& msgData = offlineMessages[i];

            std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┐" << std::endl;
            std::cout << "📨 离线消息 #" << (i + 1) << "                ━━━━━━━━━━━━━━━━━━" << std::endl;
            std::cout << "👤 来自: " << msgData.senderId << std::endl;
            std::cout << "💬 消息内容: " << msgData.content << std::endl;
            std::cout << "🕐 时间戳: " << (msgData.timestamp.empty() ? "未知时间" : msgData.timestamp) << std::endl;
            std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

            if (i < offlineMessages.size() - 1) {
                std::cout << std::endl; // 多条消息之间的分隔
            }

            // 将消息添加到队列进行历史保存，不重复显示
            MessageData displayMsg(msgData.senderId, m_userId, msgData.content);
            displayMsg.timestamp = msgData.timestamp;
            pushMessageToQueue(displayMsg, false); // 不重复计数，由登录时统一统计
        }

        std::cout << "\n✨ 所有离线消息已读取完成！以上消息来自您离线期间。您现在可以正常收发消息了。" << std::endl;

        // 更新统计信息
        m_messagesReceived += offlineMessages.size();
    } else {
        std::cout << "🎉 登录成功！欢迎使用聊天系统！" << std::endl;
    }
}

// 从捎带的登录响应中提取离线消息详情
std::vector<MessageData> ChatClientApp::extractOfflineMessageDetails(const std::string& bundledResponse) {
    std::vector<MessageData> messages;

    // 1) 找到 OFFLINE_COUNT，便于日志
    size_t countPos = bundledResponse.find("OFFLINE_COUNT:");
    if (countPos == std::string::npos) return messages;

    // 2) 从 OFFLINE_COUNT 后面开始查找每个 MESSAGE| 段
    size_t cur = bundledResponse.find("MESSAGE|", countPos);
    while (cur != std::string::npos) {
        size_t next = bundledResponse.find("MESSAGE|", cur + 1);
        std::string part;
        if (next == std::string::npos) part = bundledResponse.substr(cur);
        else part = bundledResponse.substr(cur, next - cur);

        // 3) 拆分 part（按照 '|'），注意 timestamp 可能包含空格但一般不会包含 '|'。
        std::vector<std::string> tokens;
        size_t p = 0;
        while (p < part.size()) {
            size_t q = part.find('|', p);
            if (q == std::string::npos) {
                tokens.push_back(part.substr(p));
                break;
            } else {
                tokens.push_back(part.substr(p, q - p));
                p = q + 1;
            }
        }

        // tokens 期望格式： ["MESSAGE", messageId, senderId, receiverId, content, timestamp...]
        if (tokens.size() >= 6 && tokens[0] == "MESSAGE") {
            MessageData md;
            md.messageId = tokens[1];
            md.senderId  = tokens[2];
            md.receiverId = tokens[3];
            md.content = tokens[4];
            // timestamp 可能被拆成多个 token（如果服务器意外插入了 |），把剩下拼起来
            std::string ts;
            for (size_t i = 5; i < tokens.size(); ++i) {
                if (i > 5) ts += "|";
                ts += tokens[i];
            }
            md.timestamp = ts;
            messages.push_back(md);
        } else {
            // 4) 作为后备：交给 ProtocolProcessor 尝试解析（兼容旧格式）
            MessageData md;
            if (ProtocolProcessor::deserializeMessage(part, md)) {
                messages.push_back(md);
            } else {
                std::cout << "[Client] ⚠️ 无法解析离线消息段（跳过）: " << part << std::endl;
            }
        }

        cur = next;
    }

    return messages;
}



void ChatClientApp::pushMessageToQueue(const MessageData &msg, bool updateStats) {
    m_messageQueue.push(msg);
    if (updateStats) m_messagesReceived++;
}

// UI and Menu Functions
void ChatClientApp::showMenu() {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "🏠 当前状态: ";
    std::cout << "用户[" << m_userId << "] ";
    std::cout << (m_connected ? "🟢已连接" : "🔴未连接");
    std::cout << " 服务器[127.0.0.1:8080]" << std::endl;
    std::cout << "📊 统计信息: 接收(" << m_messagesReceived << ") 发送(" << m_messagesProcessed << ")";
    std::cout << std::endl << std::string(60, '=') << std::endl;

    std::cout << "\n💬 消息功能" << std::endl;
    std::cout << "   [1] 📨 发送私人消息" << std::endl;
    std::cout << "   [2] 👥 发送群组消息" << std::endl;
    std::cout << "   [3] 📬 查看接收消息" << std::endl;
    std::cout << "\n🎪 功能演示" << std::endl;
    std::cout << "   [4] 🤖 微信服务演示" << std::endl;
    std::cout << "\n🛠️ 系统管理" << std::endl;
    std::cout << "   [5] 📊 显示平台信息" << std::endl;
    std::cout << "   [6] 🔌 断开连接" << std::endl;
    std::cout << "\n🧪 测试功能" << std::endl;
    std::cout << "   [7] ⚡ ThreadPool批量测试" << std::endl;
    std::cout << "\n🚪 系统操作" << std::endl;
    std::cout << "   [0] 🔚 退出程序" << std::endl;
    std::cout << "\n" << std::string(60, '-') << std::endl;
    std::cout << "请选择功能编号: ";
}

void ChatClientApp::processMenuCommand(int option) {
    switch (option) {
        case 0: if (m_connected) disconnect(); m_running = false; break;
        case 1: sendPrivateMessage(); break;
        case 2: sendGroupMessage(); break;
        case 3: receiveMessages(); break;
        case 4: wxServiceDemo(); break;
        case 5: showPlatformInfo(); break;
        case 6: if (m_connected) disconnect(); break;
        case 7: threadPoolBatchTest(); break;
        default: std::cout << "❌ 无效选项 '" << option << "'，请重新选择（0-7）。" << std::endl;
    }
}

// Main Application Loop
void ChatClientApp::run() {
    bool showMenuInThisLoop = true;

    // 在第一次显示菜单前稍等一下，让任何离线消息处理完成
    static bool firstRun = true;
    if (firstRun) {
        #ifdef _WIN32
        Sleep(500);  // 等待服务器线程和消息处理完全启动
        #else
        usleep(500 * 1000);
        #endif
        firstRun = false;
        showMenuInThisLoop = true;
    }

    try {
        while (m_running) {
            if (showMenuInThisLoop) {
                showMenu();
                showMenuInThisLoop = false;
            }
            std::cout.flush();

            if (_kbhit()) {
                int option = getMenuChoice();
                if (option != -1) {
                    processMenuCommand(option);
                    showMenuInThisLoop = (option != 0);

                    // 执行命令后稍等一下，确保消息处理完成后再显示菜单
                    if (option != 0 && option != 6) {  // 除了退出和断开连接
                        #ifdef _WIN32
                        Sleep(100);  // 短暂延迟
                        #else
                        usleep(100 * 1000);
                        #endif
                    }
                } else {
                    std::cout << "请键入有效的数字选项。" << std::endl;
                    showMenuInThisLoop = true;
                }
            } else {
                sleepAndCheckConnection();
            }
        }
    } catch (const std::exception &e) {
        std::cout << "\n程序错误: " << e.what() << std::endl;
        if (m_connected) disconnect();
    }
}

// Message Handling Functions
void ChatClientApp::sendPrivateMessage() {
    if (!m_connected) {
        std::cout << "未连接到服务器" << std::endl;
        return;
    }
    std::string targetUser, message;
    std::cout << "请输入目标用户ID：";
    std::getline(std::cin >> std::ws, targetUser);
    std::cout << "请输入消息：";
    std::getline(std::cin >> std::ws, message);

    MessageData msgData = createMessageDataWithId(m_userId, targetUser, message);
    std::string msgFormat = ProtocolProcessor::serializeMessage(msgData);
    std::cout << (m_socket.sendPipeMessage(msgFormat) ?
                  "私人消息已发送至" + targetUser : "发送消息失败") << std::endl;
}

void ChatClientApp::sendGroupMessage() {
    if (!m_connected) {
        std::cout << "未连接到服务器" << std::endl;
        return;
    }
    std::string groupId, message;
    std::cout << "请输入群组ID：";
    std::getline(std::cin >> std::ws, groupId);
    std::cout << "请输入消息：";
    std::getline(std::cin >> std::ws, message);

    MessageData msgData = createMessageDataWithId(m_userId, groupId, message);
    std::string msgFormat = ProtocolProcessor::serializeMessage(msgData);
    std::cout << (m_socket.sendPipeMessage(msgFormat) ?
                  "群组消息已发送至" + groupId : "发送消息失败") << std::endl;
}

void ChatClientApp::receiveMessages() {
    if (!m_connected) {
        std::cout << "未连接到服务器" << std::endl;
        return;
    }

    std::cout << "\n🔍 正在查看接收消息..." << std::endl;

    // 临时创建一个消息显示函数来显示队列中的消息
    if (!m_messageQueue.hasMessage()) {
        std::cout << "\n📭 目前没有任何历史消息。如果您刚刚登录，请等待离线消息处理完成。" << std::endl;
        std::cout << "\n💡 提示：离线消息通常在登录时自动处理和显示。" << std::endl;
        return;
    }

    std::cout << "\n📬 显示历史消息：" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    int messageCount = 0;
    const int maxMessages = 10; // 最多显示10条消息

    while (messageCount < maxMessages) {
        MessageData msg;
        if (m_messageQueue.pop(msg, 5)) {  // 较短的等待时间
            if (msg.senderId != "SERVER" || !msg.content.empty()) {
                messageCount++;
                std::cout << "\n[" << messageCount << "] 📨 来自: " << msg.senderId << std::endl;
                std::cout << "     💬 内容: " << msg.content << std::endl;
                if (!msg.timestamp.empty()) {
                    std::cout << "     🕐 时间: " << msg.timestamp << std::endl;
                }
                std::cout << std::string(50, '-') << std::endl;
            }
        } else {
            break; // 没有更多消息
        }
    }

    if (messageCount == 0) {
        std::cout << "\n📭 目前没有任何历史消息。" << std::endl;
    } else {
        std::cout << "\n✅ 已显示 " << messageCount << " 条消息" << std::endl;
        std::cout << "\n💡 提示：消息显示完成后会被移除队列，请及时查看重要信息。" << std::endl;
    }

    std::cout << "\n按任意键返回主菜单..." << std::endl;
    if (_kbhit()) {
        _getch(); // 消耗按键输入
    }
}

void ChatClientApp::wxServiceDemo() {
    if (m_wxService) {
        std::cout << "\n=== 微信服务演示 ===" << std::endl;
        m_wxService->groupFeatureDemo();
        std::cout << "当前登录用户：" << (m_wxService->loggedIn() ? m_wxService->userId() : "未登录") << std::endl;
    } else {
        std::cout << "微信服务未初始化" << std::endl;
    }
}

void ChatClientApp::showPlatformInfo() {
    std::cout << "\n=== 平台信息 ===" << std::endl;
    std::cout << "当前用户：" << m_userId << std::endl;
    std::cout << "连接状态：" << (m_connected ? "已连接" : "未连接") << std::endl;
    std::cout << "平台用户数：" << m_platform.users.size() << std::endl;
    std::cout << "平台群组数：" << m_platform.groups.size() << std::endl;

    std::cout << "\n用户列表：" << std::endl;
    for (const auto &user : m_platform.users)
        std::cout << "  - " << user.first << ": " << user.second.nickname() << std::endl;

    std::cout << "\n群组列表：" << std::endl;
    for (const auto &group : m_platform.groups)
        std::cout << "  - " << group.first << " (" 
                  << (group.second.type() == GroupType::QQ ? "QQ" : "WeChat")
                  << ") - 群主: " << group.second.owner() << std::endl;
}

void ChatClientApp::threadPoolBatchTest() {
    if (!m_connected) {
        std::cout << "未连接到服务器，无法进行批量发送测试" << std::endl;
        return;
    }

    std::cout << "\n=== ThreadPool批量消息发送测试（并发模式）===" << std::endl;
    std::cout << "ThreadPool已启动，准备批量发送消息..." << std::endl;

    std::string targetUser;
    std::cout << "请输入目标用户ID (默认: alice)：";
    std::getline(std::cin >> std::ws, targetUser);
    if (targetUser.empty()) targetUser = "alice";

    int messageCount = 5;
    std::cout << "请输入发送消息数量 (默认: 5)：";
    std::string countStr;
    std::getline(std::cin >> std::ws, countStr);
    if (!countStr.empty()) {
        try { messageCount = std::stoi(countStr); } catch (...) {}
        if (messageCount < 1) messageCount = 1;
        if (messageCount > 10) messageCount = 10;
    }

    std::cout << "[ThreadPool] 准备向用户 " << targetUser << " 发送 " << messageCount << " 条批量消息" << std::endl;
    std::cout << "[ThreadPool] 消息将顺序发送（避免socket竞争），显示发送结果..." << std::endl;

    std::vector<std::shared_ptr<BatchMessageTask>> tasks;
    for (int i = 0; i < messageCount; ++i) {
        tasks.push_back(std::make_shared<BatchMessageTask>(
            m_socket, m_socketMutex, targetUser, m_userId, messageCount, i));
    }

    std::cout << "[ThreadPool] 并发提交 " << messageCount << " 个任务..." << std::endl;
    for (auto &task : tasks) {
        m_threadPool.submit(std::static_pointer_cast<TaskBase>(task));
    }

    std::cout << "[ThreadPool] 等待所有任务完成..." << std::endl;
    m_threadPool.waitForCompletion();

    int successCount = 0, failCount = 0;
    for (auto &task : tasks) {
        successCount += (task->getStatus() == TaskStatus::COMPLETED) ? 1 : 0;
        failCount += (task->getStatus() != TaskStatus::COMPLETED) ? 1 : 0;
    }

    std::cout << "\n=== 批量发送完成 ===" << std::endl;
    std::cout << "总消息数: " << messageCount << std::endl;
    std::cout << "成功发送: " << successCount << std::endl;
    std::cout << "失败发送: " << failCount << std::endl;
    std::cout << "成功率: " << (messageCount > 0 ? (successCount * 100.0 / messageCount) : 0) << "%" << std::endl;
    std::cout << "连接状态: " << (m_connected ? "正常" : "已断开") << std::endl;
    std::cout << "ThreadPool测试完成" << std::endl;
}
