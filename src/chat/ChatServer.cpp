#include "ChatServer.hpp"
#include "../common/Protocol.hpp"
#include "../core/Message.hpp"
#include <map>
#include <iostream>
#include <sstream>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <condition_variable>


ChatServer::ChatServer(Platform& pf) : m_platform(pf), m_running(false) {
    // 确保数据文件存在，如果不存在则初始化
    std::cout << "[ChatServer] 初始化聊天服务器..." << std::endl;

    // 检查并加载用户和群组数据
    if (!m_platform.load("data/users.txt", "data/groups.txt")) {
        std::cout << "[ChatServer] 警告：数据文件加载失败，使用默认数据" << std::endl;
    }
}

ChatServer::~ChatServer() {
    stop();

    std::cout << "[ChatServer] 服务器正在关闭，保存数据..." << std::endl;
    if (!m_platform.save("data/users.txt", "data/groups.txt")) {
        std::cout << "[ChatServer] 警告：数据保存失败" << std::endl;
    }
}

bool ChatServer::start(uint16_t port) {
    if (m_running) {
        std::cout << "服务器已在运行" << std::endl;
        return true;
    }

    if (!m_serverSocket.init()) {
        std::cerr << "初始化服务器失败: " << m_serverSocket.getLastError() << std::endl;
        return false;
    }

    if (!m_serverSocket.create()) {
        std::cerr << "创建服务器套接字失败: " << m_serverSocket.getLastError() << std::endl;
        m_serverSocket.cleanup();
        return false;
    }

    if (!m_serverSocket.bind(port)) {
        std::cerr << "绑定端口失败: " << m_serverSocket.getLastError() << std::endl;
        m_serverSocket.close();
        m_serverSocket.cleanup();
        return false;
    }

    if (!m_serverSocket.listen()) {
        std::cerr << "监听连接失败: " << m_serverSocket.getLastError() << std::endl;
        m_serverSocket.close();
        m_serverSocket.cleanup();
        return false;
    }

    m_running = true;
    std::cout << "服务器启动成功，监听端口 " << port << std::endl;
    std::cout << "等待客户端连接..." << std::endl;

    return true;
}

void ChatServer::stop() {
    if (m_running) {
        m_running = false;
        m_serverSocket.close();
        m_serverSocket.cleanup();

        // 清理所有客户端连接
        for (auto client : m_activeClients) {
            if (client) {
                client->socket.close();
                delete client;
            }
        }
        m_activeClients.clear();

        std::cout << "服务器已停止" << std::endl;
    }
}



void ChatServer::acceptNewClient() {
    std::cout << "等待客户端连接..." << std::endl;

    std::string clientIp;
    uint16_t clientPort;
    SocketHandle clientSocket = m_serverSocket.accept(clientIp, clientPort);

    if (clientSocket == -1) {
        std::cerr << "接受连接失败: " << m_serverSocket.getLastError() << std::endl;
        return;
    }

    // 创建新的客户端会话，拥有真实的socket
    TcpSocket realSocket;
    realSocket.setHandle(clientSocket);
    ClientSession* newClient = new ClientSession(clientIp, clientPort, std::move(realSocket));

    m_activeClients.push_back(newClient);

    std::cout << "新客户端连接成功: " << clientIp << ":" << clientPort << " (socket已初始化)" << std::endl;
}

void ChatServer::handleClientMessages() {
    std::cout << "处理客户端消息..." << std::endl;

    if (m_activeClients.empty()) {
        std::cout << "没有活动客户端" << std::endl;
        return;
    }

    // 处理所有客户端的消息
    for (auto client : m_activeClients) {
        if (!client) {
            continue;
        }

        if (!client->socket.isConnected()) {
            std::cout << "客户端 " << client->ip << ":" << client->port << " 连接已断开，移除客户端" << std::endl;
            // 断开连接的客户端从列表移除
            for (auto it = m_activeClients.begin(); it != m_activeClients.end(); ++it) {
                if (*it == client) {
                    m_activeClients.erase(it);
                    delete client;
                    break;
                }
            }
            continue;
        }

        std::string message;
        ssize_t bytesRead = client->socket.recv(message, 1024);

        if (bytesRead > 0) {
            std::cout << "[" << client->ip << ":" << client->port << "] 收到消息: " << message << std::endl;

            std::string response = processMessage(message, client, m_activeClients);

            // 发送响应前验证连接
            if (client && client->socket.isConnected()) {
                if (client->socket.send(response)) {
                    std::cout << "[" << client->ip << ":" << client->port << "] 响应发送成功" << std::endl;
                } else {
                    std::cout << "[" << client->ip << ":" << client->port << "] 响应发送失败: " << client->socket.getLastError() << std::endl;
                }
            }
        }
        else if (bytesRead == 0) {
            std::cout << "客户端连接已关闭: " << client->ip << ":" << client->port << " (正常断开)" << std::endl;
            // 标记客户端为断开状态
            client->isLoggedIn = false;
            client->socket.close();
        }
        else {
            // 连接错误，检查错误码决定是否断开
            int errorCode = client->socket.getLastErrorCode();
            if (errorCode == 10054 || errorCode == 104 || errorCode == 10053) {  // 连接重置或已被破坏
                std::cout << "客户端连接已断开 (Connection reset): " << client->ip << ":" << client->port << std::endl;
                client->isLoggedIn = false;
                client->socket.close();
            } else if (errorCode == 10060) {  // 超时
                // 超时忽略
            } else {
                std::cout << "客户端连接错误 (Error " << errorCode << "): " << client->ip << ":" << client->port << std::endl;
                client->isLoggedIn = false;
                client->socket.close();
            }
        }
    }
}



// 创建用户会话
ChatServer::ClientSession* ChatServer::createSession(const std::string& ip, uint16_t port, TcpSocket&& socket) {
    ClientSession* newSession = new ClientSession(ip, port, std::move(socket));
    m_activeClients.push_back(newSession);
    return newSession;
}

std::string ChatServer::processMessage(const std::string& rawMessage, const std::string& clientId) {
    ClientSession* currentClient = findClientByAddr(clientId);

    if (rawMessage.substr(0, 5) == "LOGIN") {
        if (currentClient == nullptr) {
            // 这个分支应该不会执行，如果执行说明acceptNewClient有问题
            std::cout << "[LOGIN WARNING] 未找到现有会话，acceptNewClient可能未执行" << std::endl;
            size_t colon = clientId.find(':');
            if (colon != std::string::npos) {
                std::string ip = clientId.substr(0, colon);
                uint16_t port = (uint16_t)std::stoi(clientId.substr(colon + 1));
                TcpSocket tempSocket; // 临时socket，将由ClientHandler设置
                currentClient = new ClientSession(ip, port, std::move(tempSocket));
                m_activeClients.push_back(currentClient);
            }
        }

        // 为现有或新建的会话设置用户信息
        if (currentClient) {
            size_t pos = rawMessage.find('|');
            if (pos != std::string::npos) {
                std::string userId = rawMessage.substr(pos + 1);
                currentClient->userId = userId;
                currentClient->isLoggedIn = true;
                std::cout << "[登录] 用户 " << userId << " 已成功登录，会话已激活 (IP: "
                          << currentClient->ip << ":" << currentClient->port << ")" << std::endl;
            }
        }

        return processMessage(rawMessage, currentClient, m_activeClients);
    }

    // 对于非LOGIN消息，使用现有逻辑
    return processMessage(rawMessage, currentClient, m_activeClients);
}

std::string ChatServer::processMessage(const std::string& rawMessage, ClientSession* currentClient, std::vector<ClientSession*>& activeClients) {
    // 先处理ACK消息
    if (rawMessage.substr(0, 3) == "ACK") {
        handleAck(rawMessage, currentClient);
        return "";  // ACK不需要响应
    }

    // 消息处理逻辑
    if (!currentClient) {
        return "RESPONSE|ERROR|LOGIN_FAILED|登录失败：服务器内部错误，请稍后重试";
    }

    if (rawMessage.substr(0, 5) == "LOGIN") {
        // 解析登录消息格式: LOGIN|userId
        size_t pos = rawMessage.find('|');
        std::string userId = (pos != std::string::npos) ? rawMessage.substr(pos + 1) : "";

        if (!userId.empty()) {
            // 直接设置当前客户端的登录状态
            currentClient->isLoggedIn = true;
            currentClient->userId = userId;
            std::cout << "[登录] 用户 " << userId << " 已成功登录 (IP: " << currentClient->ip << ":" << currentClient->port << ")" << std::endl;

            // 登录成功后，捎带离线消息数据
            return createBundledLoginResponse(userId);
        } else {
            std::cout << "[登录错误] 无效的用户ID" << std::endl;
            return "RESPONSE|ERROR|LOGIN_FAILED|登录失败：无效的用户ID";
        }
    }
    else if (rawMessage.substr(0, 7) == "MESSAGE") {
        MessageData msgData;
        if (!ProtocolProcessor::deserializeMessage(rawMessage, msgData)) {
            std::cout << "[协议错误] 无法解析消息: " << rawMessage << std::endl;
            return "RESPONSE|ERROR|PROTOCOL_ERROR|消息格式错误，请检查协议版本";
        }

        std::string senderId = msgData.senderId;
        std::string recipientId = msgData.receiverId;
        std::string content = msgData.content;

        if (!senderId.empty() && !recipientId.empty()) {
            // 检查接收方是否在线
            if (isUserOnline(recipientId)) {
                // 接收方在线，直接转发消息并等待ACK
                ClientSession* recipientSession = findUserById(recipientId);
                if (recipientSession && recipientSession->socket.isConnected()) {
                    std::string forwardMessage = ProtocolProcessor::serializeMessage(msgData);
                    bool success = sendMessageWithAck(recipientSession, forwardMessage);

                    if (success) {
                        std::cout << "[消息转发] ✅ 消息成功转发并确认至用户 " << recipientId << std::endl;
                        return "RESPONSE|SUCCESS|MESSAGE_SENT|消息已发送并确认";
                    } else {
                        storeOfflineMessage(recipientId, forwardMessage);
                        std::cout << "[离线消息] 转发失败，已保存为离线消息，发送者: " << senderId << std::endl;
                        return "RESPONSE|ERROR|SEND_FAILED|转发失败，已保存为离线消息";
                    }
                } else {
                    storeOfflineMessage(recipientId, ProtocolProcessor::serializeMessage(msgData));
                    std::cout << "[离线消息] 接收者连接异常，已保存为离线消息，发送者: " << senderId << std::endl;
                    return "RESPONSE|SUCCESS|MESSAGE_CACHED|接收者连接异常，已保存为离线消息";
                }
            } else {
                // 接收方不在线，缓存消息
                storeOfflineMessage(recipientId, ProtocolProcessor::serializeMessage(msgData));
                std::cout << "[离线缓存] 接收方不在线，已缓存消息给用户 " << recipientId << std::endl;
                return "RESPONSE|SUCCESS|MESSAGE_CACHED|消息已缓存";
            }
        }
        std::cout << "[格式错误] 消息格式错误: " << rawMessage << std::endl;
        return "RESPONSE|ERROR|INVALID_FORMAT|消息格式无效";
    }
    else if (rawMessage.substr(0, 6) == "LOGOUT") {
        if (currentClient) {
            std::string userId = currentClient->userId;
            currentClient->isLoggedIn = false;
            std::cout << "[登出] 用户 " << userId << " 已成功登出" << std::endl;
        }
        return "RESPONSE|SUCCESS|LOGOUT_OK|登出成功";
    }

    return "RESPONSE|ERROR|UNKNOWN_COMMAND|未知命令";
}

void ChatServer::broadcastToUser(const std::string& userId, const struct Message& msg) {
    // 简单的广播逻辑
    std::cout << "[广播] 消息转发至用户 " << userId << ": " << msg.content << std::endl;
}

void ChatServer::broadcastToGroup(const std::string& groupId, const struct Message& msg) {
    // 简单的群组广播逻辑
    std::cout << "[群组广播] 消息转发至群组 " << groupId << ": " << msg.content << std::endl;
}

std::string ChatServer::serializeMessage(const struct Message& msg) {
    return "MESSAGE|" + msg.fromId + "|" + msg.toId + "|" + msg.content + "|" + msg.getFormattedTime();
}

// 创建捎带离线消息的登录响应
std::string ChatServer::createBundledLoginResponse(const std::string& userId) {
    auto it = m_offlineMessages.find(userId);
    if (it == m_offlineMessages.end() || it->second.empty()) {
        std::cout << "[登录捎带] 用户 " << userId << " 没有离线消息" << std::endl;
        return "RESPONSE|SUCCESS|LOGIN_OK|登录成功";
    }

    // 限制离线消息数量以避免响应太长
    const size_t MAX_OFFLINE_MESSAGES = 50;
    size_t actualCount = std::min(it->second.size(), MAX_OFFLINE_MESSAGES);

    std::cout << "[登录捎带] 用户 " << userId << " 有 " << it->second.size() << " 条离线消息，捎带前 " << actualCount << " 条" << std::endl;

    // 构建捎带响应
    std::string bundledResponse = "RESPONSE|SUCCESS|LOGIN_OK|登录成功|OFFLINE_COUNT:" +
                                  std::to_string(actualCount) + "|";

    // 捎带离线消息
    size_t messageIndex = 0;
    for (const auto& offlineMsg : it->second) {
        if (messageIndex >= actualCount) {
            break; // 只发送指定数量的消息
        }
        if (messageIndex > 0) {
            bundledResponse += "|"; // 分隔符
        }
        bundledResponse += offlineMsg;
        messageIndex++;
    }

    std::cout << "[登录捎带] 构造的捎带响应长度: " << bundledResponse.length() << std::endl;
    std::cout << "[登录捎带] 第一个消息预览: " << bundledResponse.substr(0, 200) << (bundledResponse.length() > 200 ? "..." : "") << std::endl;

    // 清空该用户的离线消息队列(已处理的消息)
    if (actualCount >= it->second.size()) {
        // 所有消息都已发送，清空队列
        it->second.clear();
        m_offlineMessages.erase(it);
        std::cout << "[登录捎带] 已清空用户 " << userId << " 的离线消息队列" << std::endl;
    } else {
        // 移除已发送的消息，保留剩余消息
        auto& messageQueue = it->second;
        for (size_t i = 0; i < actualCount; ++i) {
            if (!messageQueue.empty()) {
                messageQueue.pop_front();
            }
        }
        std::cout << "[登录捎带] 用户 " << userId << " 还剩余 " << messageQueue.size() << " 条离线消息在队列中" << std::endl;
    }

    return bundledResponse;
}

// 离线消息处理的辅助方法实现
ChatServer::ClientSession* ChatServer::findUserById(const std::string& userId) {
    for (auto client : m_activeClients) {
        if (client && client->userId == userId && client->isLoggedIn) {
            return client;
        }
    }
    return nullptr;
}

bool ChatServer::isUserOnline(const std::string& userId) {
    return findUserById(userId) != nullptr;
}

void ChatServer::storeOfflineMessage(const std::string& recipientId, const std::string& message) {
    // 检查消息是否为有效的MESSAGE格式
    if (message.substr(0, 7) != "MESSAGE") {
        std::cout << "[离线消息] 警告：尝试存储非MESSAGE格式的离线消息，已忽略" << std::endl;
        return;
    }

    // 检查当前用户离线消息数量上限
    const size_t MAX_OFFLINE_PER_USER = 100;
    auto& userMessages = m_offlineMessages[recipientId];

    if (userMessages.size() >= MAX_OFFLINE_PER_USER) {
        // 移除最老的消息，为新消息腾出空间
        std::cout << "[离线消息] 用户 " << recipientId << " 的离线消息数量达到上限 (" << MAX_OFFLINE_PER_USER << ")，移除最老的消息" << std::endl;
        userMessages.pop_front();
    }

    // 将消息存储到全局离线消息队列中
    userMessages.push_back(message);
    std::cout << "[离线消息] 消息已缓存给用户 " << recipientId
              << "，当前队列长度: " << userMessages.size()
              << "，消息预览: " << message.substr(0, 80) << (message.length() > 80 ? "..." : "") << std::endl;

    // 检查总的离线消息缓存是否过多
    size_t totalMessages = 0;
    for (const auto& userQueue : m_offlineMessages) {
        totalMessages += userQueue.second.size();
    }

    if (totalMessages % 50 == 0) {  // 每50条消息输出一次统计信息
        std::cout << "[离线消息统计] 当前系统离线消息总数: " << totalMessages
                  << "，分布在 " << m_offlineMessages.size() << " 个用户中" << std::endl;
    }
}

void ChatServer::deliverOfflineMessages(const std::string& userId) {
    auto it = m_offlineMessages.find(userId);
    if (it != m_offlineMessages.end() && !it->second.empty()) {
        ClientSession* client = findUserById(userId);
        if (client && client->socket.isConnected()) {
            std::cout << "[离线消息] 向用户 " << userId << " 投递 " << it->second.size() << " 条离线消息" << std::endl;

            // 创建离线消息通知
            std::string offlineNotify = "RESPONSE|OFFLINE_MESSAGES|COUNT|" + std::to_string(it->second.size()) + "|离线消息准备投递";

            // 发送离线消息通知(不需要ACK，直接发送)
            if (!client->socket.sendPipeMessage(offlineNotify)) {
                std::cout << "[离线消息] 离线消息通知发送失败，跳过离线消息投递" << std::endl;
                return;
            }
            std::cout << "[离线消息] 已发送离线消息通知给用户" << std::endl;

            // 依次投递离线消息，每条都等待ACK确认(为MESSAGE类型启用ACK)
            size_t delivered = 0;
            size_t failed = 0;

            while (!it->second.empty() && failed < 3) {  // 最多连续失败3次就停止
                std::string offlineMsg = it->second.front();
                std::cout << "[离线消息] 投递 (" << (delivered + 1) << "/" << it->second.size() << "): " << offlineMsg << " 到用户 " << userId << std::endl;

                if (sendMessageWithAck(client, offlineMsg)) {
                    std::cout << "[离线消息] ✅ 消息确认收到" << std::endl;
                    it->second.pop_front();
                    delivered++;
                    failed = 0;  // 重置失败计数
                } else {
                    std::cout << "[离线消息] ❌ 消息投递失败，ACK超时" << std::endl;
                    failed++;

                    if (failed >= 3) {
                        std::cout << "[离线消息] 连续失败3次，停止投递剩余离线消息" << std::endl;
                        break;
                    }

                    // 短暂延迟后重试
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }

            std::cout << "[离线消息] 成功投递 " << delivered << " 条离线消息给用户 " << userId << ", 总共有 " << it->second.size() << " 条剩余未投递" << std::endl;

            // 如果队列为空，则移除该用户条目
            if (it->second.empty()) {
                m_offlineMessages.erase(it);
            }
        }
    }
}

// 查找客户端通过IP:port
ChatServer::ClientSession* ChatServer::findClientByAddr(const std::string& addr) {
    size_t colon = addr.find(':');
    if (colon == std::string::npos) {
        return nullptr;
    }

    std::string ip = addr.substr(0, colon);
    std::string portStr = addr.substr(colon + 1);
    uint16_t port = 0;

    try {
        port = (uint16_t)std::stoi(portStr);
    } catch (const std::exception&) {
        return nullptr;
    }

    for (auto client : m_activeClients) {
        if (client && client->ip == ip && client->port == port) {
            return client;
        }
    }
    return nullptr;
}

// 简化消息处理接口
bool ChatServer::receiveFromClient(void* sessionPtr, std::string& message, bool& active) {
    ClientSession* session = reinterpret_cast<ClientSession*>(sessionPtr);
    if (!session || !session->socket.isConnected()) {
        active = false;
        return false;
    }

    message.clear();
    return session->socket.receivePipeMessage(message, 1);
}

bool ChatServer::sendToClient(void* sessionPtr, const std::string& response) {
    ClientSession* session = reinterpret_cast<ClientSession*>(sessionPtr);
    if (!session || !session->socket.isConnected()) {
        return false;
    }
    return session->socket.sendPipeMessage(response);
}

// 发送消息并等待ACK确认
bool ChatServer::sendMessageWithAck(ClientSession* targetClient, const std::string& message) {
    if (!targetClient || !targetClient->socket.isConnected()) {
        return false;
    }

    std::string messageToSend = message;
    std::string messageId;

    // 解析消息类型
    Type msgType = ProtocolProcessor::parseProtocolType(message);

    if (msgType == Message) {
        // 处理MESSAGE类型 - 检查并设置messageId
        MessageData msgData;
        if (!ProtocolProcessor::deserializeMessage(message, msgData)) {
            std::cout << "[协议错误] 无法解析MESSAGE类型消息: " << message << std::endl;
            return false;
        }

        if (msgData.messageId.empty()) {
            msgData.messageId = ProtocolProcessor::generateMessageId();
            messageToSend = ProtocolProcessor::serializeMessage(msgData);
            std::cout << "[消息ID] 生成MESSAGE新ID: " << msgData.messageId << std::endl;
        } else {
            std::cout << "[消息ID] 使用MESSAGE现有ID: " << msgData.messageId << std::endl;
        }
        messageId = msgData.messageId;

    } else if (msgType == Response) {
        // 处理RESPONSE类型 - 发送但不等待ACK确认
        std::cout << "[响应消息] 发送RESPONSE类型消息但不等待ACK: " << message << std::endl;

    } else {
        // 其他不需要ACK的消息类型，直接发送
        std::cout << "[直接发送] 无需ACK的消息类型，直接发送" << std::endl;
        return targetClient->socket.sendPipeMessage(message);
    }

    // 发送消息
    if (!targetClient->socket.sendPipeMessage(messageToSend)) {
        std::cout << "[发送失败] 消息发送失败，接收者: " << targetClient->userId << std::endl;
        return false;
    }

    // 对于非MESSAGE类型，直接返回成功（不需要ACK）
    if (msgType != Message) {
        std::cout << "[直接成功] 响应消息发送完成" << std::endl;
        return true;
    }

    // 创建传输记录并等待ACK（仅对MESSAGE类型）
    auto transmission = std::make_unique<MessageTransmission>(
        messageId, messageToSend, targetClient
    );

    std::unique_lock<std::mutex> lock(transmission->mutex);

    // 将传输记录添加到待处理的映射中
    m_pendingTransmissions[messageId] = std::move(transmission);

    // 等待ACK确认，最多等待3秒
    auto& trans = *m_pendingTransmissions[messageId];
    bool ackReceived = trans.cv.wait_for(lock, std::chrono::seconds(3),
                                           [&]() { return trans.acknowledged; });

    // 无论是否有ACK，都从待处理映射中移除
    m_pendingTransmissions.erase(messageId);

    if (ackReceived) {
        std::cout << "[ACK成功] 消息 " << messageId << " 已确认收到" << std::endl;
        return true;
    } else {
        std::cout << "[ACK超时] 消息 " << messageId << " 未收到确认，重试后续消息失败时将保存为离线" << std::endl;
        return false;
    }
}

// 处理ACK确认
void ChatServer::handleAck(const std::string& ackMessage, ClientSession* senderClient) {
    AckData ackData;
    if (!ProtocolProcessor::deserializeAck(ackMessage, ackData)) {
        std::cout << "[协议错误] 无法解析ACK消息: " << ackMessage << std::endl;
        return;
    }

    if (ackData.receiverId != senderClient->userId) {
        std::cout << "[ACK异常] 用户 " << senderClient->userId << " 确认其他用户的消息，消息ID: " << ackData.messageId << std::endl;
        return;
    }

    // 查找并确认对应的传输记录
    auto it = m_pendingTransmissions.find(ackData.messageId);
    if (it != m_pendingTransmissions.end()) {
        std::unique_lock<std::mutex> lock(it->second->mutex);
        it->second->acknowledged = true;
        it->second->cv.notify_one();

        std::cout << "[ACK接收] 消息 " << ackData.messageId << " 已确认，由用户 " << ackData.receiverId << " 发送" << std::endl;
    } else {
        std::cout << "[ACK无记录] 找到未知消息ID的ACK: " << ackData.messageId << std::endl;
    }
}

// 处理重试传输
void ChatServer::processRetryTransmissions() {
    if (m_pendingTransmissions.empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> completedTransmissions;

    for (auto& pair : m_pendingTransmissions) {
        std::unique_lock<std::mutex> lock(pair.second->mutex);

        // 检查是否已经确认
        if (pair.second->acknowledged) {
            completedTransmissions.push_back(pair.first);
            continue;
        }

        // 检查重试时间
        if (now >= pair.second->nextRetryTime) {
            if (pair.second->retryCount >= MAX_RETRIES) {
                std::cout << "[重试失败] 消息 " << pair.first << " 已达到最大重试次数，保存为离线消息" << std::endl;
                // 将消息保存为离线消息
                MessageData msgData;
                if (ProtocolProcessor::deserializeMessage(pair.second->content, msgData)) {
                    storeOfflineMessage(msgData.receiverId, pair.second->content);
                }
                completedTransmissions.push_back(pair.first);
                continue;
            }

            // 尝试重新发送
            auto& transmission = pair.second;
            if (transmission->targetClient->socket.isConnected()) {
                if (transmission->targetClient->socket.sendPipeMessage(transmission->content)) {
                    transmission->retryCount++;
                    transmission->nextRetryTime = now + std::chrono::milliseconds(RETRY_INTERVAL_MS * transmission->retryCount);

                    std::cout << "[重试发送] 消息 " << pair.first << " 重试第 " << transmission->retryCount << " 次" << std::endl;
                } else {
                    std::cout << "[重试失败] 消息 " << pair.first << " 重试发送失败" << std::endl;
                    transmission->retryCount++;
                    transmission->nextRetryTime = now + std::chrono::milliseconds(RETRY_INTERVAL_MS * transmission->retryCount);
                }
            } else {
                std::cout << "[重试取消] 目标客户端已断开，消息 " << pair.first << " 保存为离线" << std::endl;
                MessageData msgData;
                if (ProtocolProcessor::deserializeMessage(pair.second->content, msgData)) {
                    storeOfflineMessage(msgData.receiverId, pair.second->content);
                }
                completedTransmissions.push_back(pair.first);
            }
        }
    }

    // 清理已完成/失败的传输
    for (const auto& msgId : completedTransmissions) {
        m_pendingTransmissions.erase(msgId);
    }
}

// 清理超时传输
void ChatServer::cleanupTimeoutTransmissions() {
    if (m_pendingTransmissions.empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto maxWaitTime = std::chrono::minutes(5);  // 最大等待时间5分钟

    std::vector<std::string> timeoutTransmissions;

    for (const auto& pair : m_pendingTransmissions) {
        const auto& transmission = pair.second;
        auto elapsedTime = now - transmission->nextRetryTime;

        if (elapsedTime > maxWaitTime) {
            timeoutTransmissions.push_back(pair.first);
        }
    }

    // 清理超时传输
    for (const auto& msgId : timeoutTransmissions) {
        std::cout << "[清理超时] 超时消息已清理: " << msgId << std::endl;
        m_pendingTransmissions.erase(msgId);
    }
}
