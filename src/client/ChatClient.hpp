// ========================================================================================
// 📱 ChatClient - 即时通信客户端核心模块头文件
// ========================================================================================
// 此头文件定义了ChatClientApp类及其支撑的数据结构和辅助类型
// ========================================================================================

#pragma once

// ========================================================================================
// 🎯 标准库头文件 (按字母顺序排列)
// ========================================================================================

#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// ========================================================================================
// 🔧 系统头文件
// ========================================================================================

#include <conio.h>

// ========================================================================================
// 📚 项目核心模块头文件 (按功能分组)
// ========================================================================================

// 数据模型层
#include "../core/User.hpp"
#include "../core/Group.hpp"
#include "../core/Message.hpp"
#include "../core/Platform.hpp"

// 网络通信层
#include "../network/tcp_socket.hpp"

// 基础服务层
#include "../common/Protocol.hpp"
#include "../common/ThreadPool.hpp"
#include "../common/Repository.hpp"
#include "../common/WeChatService.hpp"

// ========================================================================================
// 🔧 编译时常量配置
// ========================================================================================

namespace Config
{
    // UI交互配置
    const int MENU_CHECK_INTERVAL_MS     = 150;
    const int CONNECTION_CHECK_THRESHOLD = 60;

    // 消息处理配置
    const int OFFLINE_MESSAGE_DRAIN_ATTEMPTS = 10;
    const int LOGIN_TIMEOUT_SECONDS          = 10;

    // 系统限制配置
    const size_t MAX_MESSAGE_SIZE = 1024;
    const int    MAX_RETRIES      = 3;
    const int    RETRY_INTERVAL_MS = 1000;
} // namespace Config

// Parsed message structure
struct ParsedMessage {
    std::string type;
    std::string senderId;
    std::string receiverId;
    std::string content;
    std::string timestamp;

    ParsedMessage() = default;
    ParsedMessage(std::string t, std::string s, std::string r, std::string c, std::string ts = "")
        : type(std::move(t)), senderId(std::move(s)), receiverId(std::move(r)),
          content(std::move(c)), timestamp(std::move(ts)) {}

    bool isValid() const { return !type.empty(); }
    bool isUserMessage() const { return type == "MESSAGE"; }
    bool isSystemMessage() const { return type == "SYSTEM"; }
    bool isResponse() const { return type == "RESPONSE"; }
};

// Async Message Queue Class
class AsyncMessageQueue
{
private:
    std::queue<MessageData> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_finished = false;

public:
    void push(const MessageData &msg)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.push(msg);
        m_cv.notify_one();
    }

    bool pop(MessageData &msg, int timeoutMs = 50)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                          [this]() { return !m_queue.empty() || m_finished; }))
        {
            if (!m_queue.empty())
            {
                msg = m_queue.front();
                m_queue.pop();
                return true;
            }
        }
        return false;
    }

    bool hasMessage()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return !m_queue.empty();
    }

    void finish()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_finished = true;
        m_cv.notify_all();
    }

    bool isFinished()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_finished && m_queue.empty();
    }
};

// Main Chat Client Application Class
class ChatClientApp
{
private:
    TcpSocket m_socket;
    Platform m_platform;
    std::string m_userId;
    bool m_connected = false;
    bool m_running = true;
    ThreadPool m_threadPool;
    std::mutex m_socketMutex;
    std::thread m_listenerThread;
    AsyncMessageQueue m_messageQueue;
    std::thread m_messageProcessorThread;
    std::atomic<size_t> m_messagesReceived{0};
    std::atomic<size_t> m_messagesProcessed{0};
    WeChatService *m_wxService = nullptr;

    // Helper Methods
    ParsedMessage parseMessage(const std::string &message) const {
        if (message.empty()) return ParsedMessage("ERROR", "", "", "", "");
        std::stringstream ss(message);
        std::string type, senderId = "", receiverId = "", content = "", timestamp = "";
        if (std::getline(ss, type, '|')) {
            if (type == "MESSAGE") {
                // 新格式: MESSAGE|messageId|senderId|receiverId|content|timestamp
                std::string messageId;
                std::getline(ss, messageId, '|');  // 跳过messageId
                std::getline(ss, senderId, '|');
                std::getline(ss, receiverId, '|');
                std::getline(ss, content, '|');
                std::getline(ss, timestamp, '|');
            } else if (type == "RESPONSE") {
                std::getline(ss, senderId, '|');
                std::getline(ss, receiverId, '|');
                std::getline(ss, content, '|');
            } else {
                senderId = type;
                std::getline(ss, content, '|');
            }
        }
        return ParsedMessage(type, senderId, receiverId, content, timestamp);
    }

    MessageData createMessageData(const ParsedMessage &parsed) const {
        MessageData msg(parsed.senderId, parsed.receiverId, parsed.content);
        if (!parsed.timestamp.empty()) msg.timestamp = parsed.timestamp;
        return msg;
    }

    void displaySystemMessage(const std::string &content) {
        if (content.find("OFFLINE_MESSAGES") != std::string::npos) {
            std::stringstream ss(content);
            std::string prefix, type, count;
            std::getline(ss, prefix, '|');
            std::getline(ss, type, '|');
            std::getline(ss, count, '|');
            std::cout << "\n📨 系统通知：收到 " << count << " 条离线消息" << std::endl;
        }
    }

    void displayServerResponse(const std::string &content) {
        if (content.find("MESSAGE_SENT") != std::string::npos) {
            std::cout << "\n✅ 消息已发送成功" << std::endl;
        } else if (content.find("MESSAGE_CACHED") != std::string::npos) {
            std::cout << "\n📨 接收方不在线，已缓存消息" << std::endl;
        } else if (content.find("SEND_FAILED") != std::string::npos) {
            std::cout << "\n⚠️ 消息发送失败" << std::endl;
        }
    }

    int getMenuChoice() {
        char input = getchar();
        if (input >= '0' && input <= '9') {
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            return input - '0';
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return -1;
    }

    // 发送ACK确认消息
    bool sendAckMessage(const std::string& messageId) {
        if (!m_connected || messageId.empty()) {
            return false;
        }

        AckData ackData(messageId, m_userId);
        std::string ackMessage = ProtocolProcessor::serializeAck(ackData);

        std::lock_guard<std::mutex> lock(m_socketMutex);
        bool success = m_socket.sendPipeMessage(ackMessage);
        if (success) {
            std::cout << "[Client] ✅ 已发送ACK确认: " << messageId << std::endl;
        } else {
            std::cout << "[Client] ❌ ACK发送失败: " << messageId << std::endl;
        }
        return success;
    }

    // 创建包含消息ID的消息数据
    MessageData createMessageDataWithId(const std::string& senderId, const std::string& receiverId,
                                       const std::string& content) {
        MessageData msgData;
        msgData.messageId = ProtocolProcessor::generateMessageId();
        msgData.senderId = senderId;
        msgData.receiverId = receiverId;
        msgData.content = content;
        return msgData;
    }

    // 显示离线消息的专用函数
    void displayOfflineMessages(const std::string &bundledResponse);

    // 从捎带的登录响应中提取离线消息详情
    std::vector<MessageData> extractOfflineMessageDetails(const std::string& bundledResponse);

    void pushMessageToQueue(const MessageData &msg, bool updateStats = true);

    class BatchMessageTask : public TaskBase
    {
    private:
        TcpSocket &m_clientSocket;
        std::mutex &m_socketMutex;
        std::string m_targetUser, m_currentUser;
        int m_messageCount, m_messageId;

    public:
        BatchMessageTask(TcpSocket &socket, std::mutex &socketMutex,
                         const std::string &targetUser, const std::string &currentUser,
                         int count, int id)
            : m_clientSocket(socket), m_socketMutex(socketMutex),
              m_targetUser(targetUser), m_currentUser(currentUser),
              m_messageCount(count), m_messageId(id) {}

        void execute() override {
            status_ = TaskStatus::RUNNING;
            try {
                std::string message = "批量消息 #" + std::to_string(m_messageId + 1) +
                                      " - ThreadPool并发发送测试";
                MessageData msgData;
                msgData.messageId = ProtocolProcessor::generateMessageId();
                msgData.senderId = m_currentUser;
                msgData.receiverId = m_targetUser;
                msgData.content = message;

                std::string msgFormat = ProtocolProcessor::serializeMessage(msgData);

                if (m_clientSocket.sendPipeMessage(msgFormat)) {
                    std::cout << "[ThreadPool] 批量消息 #" << (m_messageId + 1) << " 发送成功" << std::endl;
                    status_ = TaskStatus::COMPLETED;
                    onComplete();
                } else {
                    std::cout << "[ThreadPool] 批量消息 #" << (m_messageId + 1) << " 发送失败" << std::endl;
                    status_ = TaskStatus::FAILED;
                    onError();
                }
            } catch (...) {
                status_ = TaskStatus::FAILED;
                onError();
            }
        }

        void onComplete() override {
            std::cout << "[ThreadPool] 批量消息 #" << (m_messageId + 1) << " 完成" << std::endl;
        }

        void onError() override {
            std::cout << "[ThreadPool] 批量消息 #" << (m_messageId + 1) << " 失败" << std::endl;
        }
    };

    // =============================================================================
    // 🔄 核心消息处理函数
    // =============================================================================

    void processMessageToQueue(const std::string& message);
    void messageProducer();
    void messageConsumer();
    void sleepAndCheckConnection();

public:
    ChatClientApp();
    ~ChatClientApp();

    // Platform and User Management
    void setupPlatform();
    void setUser(const std::string &userId);

    // Connection Management
    bool connect(const std::string &serverIp = "127.0.0.1", uint16_t serverPort = 8080);
    void disconnect();

    // Main Application Loop
    void run();

    // User Interface Functions
    void showMenu();
    void processMenuCommand(int option);

    // Message Handling
    void sendPrivateMessage();
    void sendGroupMessage();
    void receiveMessages();

    // Service Demonstrations
    void wxServiceDemo();
    void showPlatformInfo();
    void threadPoolBatchTest();

};
