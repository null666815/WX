#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <list>
#include <deque>
#include "../network/tcp_socket.hpp"
#include "../core/Message.hpp"
#include <condition_variable>
#include "../core/Platform.hpp"

class ChatServer {
public:
    // 客户端会话结构体
    struct ClientSession {
        TcpSocket socket;
        std::string ip;
        uint16_t port;
        std::string userId;
        bool isLoggedIn = false;
        std::deque<std::string> offlineMessages;  // 离线消息队列

        ClientSession() : port(0) {}
        ClientSession(const std::string& ipAddr, uint16_t port)
            : ip(ipAddr), port(port), isLoggedIn(false) {}
        ClientSession(const std::string& ipAddr, uint16_t port, TcpSocket&& socket)
            : ip(ipAddr), port(port), socket(std::move(socket)), isLoggedIn(false) {}
    };

public:
    ChatServer(Platform& pf);
    ~ChatServer();

    bool start(uint16_t port);
    void stop();
    bool isRunning() const { return m_running; }

    // 主循环，处理客户端连接和消息
    void run();

    // 消息处理接口
    std::string processMessage(const std::string& rawMessage, const std::string& clientId = "");
    std::string processMessage(const std::string& rawMessage, ClientSession* client, std::vector<ClientSession*>& activeClients);

    // 核心会话管理
    ClientSession* createSession(const std::string& ip, uint16_t port, TcpSocket&& socket);

    // 消息处理接口
    bool receiveFromClient(void* sessionPtr, std::string& message, bool& active);
    bool sendToClient(void* sessionPtr, const std::string& response);

private:
    // 新增的私有方法
    void acceptNewClient();
    void handleClientMessages();

    void broadcastToUser(const std::string& userId, const struct Message& msg);
    void broadcastToGroup(const std::string& groupId, const struct Message& msg);
    std::string serializeMessage(const struct Message& msg);

    // 捎带离线消息处理
    std::string createBundledLoginResponse(const std::string& userId);



    // 离线消息处理的辅助方法
    ClientSession* findUserById(const std::string& userId);
    bool isUserOnline(const std::string& userId);
    void storeOfflineMessage(const std::string& recipientId, const std::string& message);
    void deliverOfflineMessages(const std::string& userId);

    ClientSession* findClientByAddr(const std::string& addr);

    // 消息重传管理
    struct MessageTransmission {
        std::string messageId;
        std::string content;
        ClientSession* targetClient;
        int retryCount;
        std::chrono::steady_clock::time_point nextRetryTime;
        std::mutex mutex;
        bool acknowledged;
        std::condition_variable cv;

        MessageTransmission(const std::string& msgId, const std::string& msg, ClientSession* client)
            : messageId(msgId), content(msg), targetClient(client),
              retryCount(0), acknowledged(false) {
            nextRetryTime = std::chrono::steady_clock::now();
        }
    };

    std::vector<ClientSession*> m_activeClients;
    std::unordered_map<std::string, std::deque<std::string>> m_offlineMessages;  // 全局离线消息队列，按用户ID索引
    std::unordered_map<std::string, std::unique_ptr<MessageTransmission>> m_pendingTransmissions;  // 等待ACK的消息
    bool m_running;
    Platform& m_platform;
    TcpSocket m_serverSocket;

    std::unordered_map<std::string, TcpSocket*> clientSockets; // for threaded version

    static const size_t MAX_MESSAGE_SIZE = 1024;
    static const int MAX_RETRIES = 3;
    static const int RETRY_INTERVAL_MS = 1000;

    // 新增：消息传输方法
    bool sendMessageWithAck(ClientSession* targetClient, const std::string& message);
    void handleAck(const std::string& ackMessage, ClientSession* senderClient);
    void processRetryTransmissions();
    void cleanupTimeoutTransmissions();
};
