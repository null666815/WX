#include "Protocol.hpp"
#include <chrono>
#include <iomanip>
#include <atomic>

// ProtocolProcessor类的实现
std::string ProtocolProcessor::serializeMessage(const MessageData& msg) {
    if (!validateMessageFields(msg)) {
        return "";
    }

    std::ostringstream oss;
    oss << "MESSAGE|"
        << msg.messageId << "|"
        << msg.senderId << "|"
        << msg.receiverId << "|"
        << msg.content << "|"
        << (msg.timestamp.empty() ? getCurrentTimestamp() : msg.timestamp);

    return oss.str();
}

bool ProtocolProcessor::deserializeMessage(const std::string& rawData, MessageData& msg) {
    std::istringstream iss(rawData);
    std::string protocolType;

    if (!std::getline(iss, protocolType, '|') || protocolType != "MESSAGE") {
        return false;
    }

    std::string messageId, sender, receiver, content, timestamp;
    if (!std::getline(iss, messageId, '|') ||
        !std::getline(iss, sender, '|') ||
        !std::getline(iss, receiver, '|') ||
        !std::getline(iss, content, '|')) {
        return false;
    }

    // timestamp字段可选
    std::getline(iss, timestamp);

    MessageData tempMsg(messageId, sender, receiver, content);
    if (!timestamp.empty()) {
        tempMsg.timestamp = timestamp;
    } else {
        tempMsg.timestamp = getCurrentTimestamp();
    }

    if (!validateMessageFields(tempMsg)) {
        return false;
    }

    msg = tempMsg;
    return true;
}

std::string ProtocolProcessor::serializeResponse(const ResponseData& resp) {
    std::ostringstream oss;
    oss << "RESPONSE|"
        << (resp.success ? "SUCCESS" : "ERROR") << "|"
        << resp.statusCode << "|"
        << resp.message;

    // 添加额外数据（如果有）
    if (!resp.additionalData.empty()) {
        oss << "|";
        for (size_t i = 0; i < resp.additionalData.size(); ++i) {
            if (i > 0) oss << ",";
            oss << resp.additionalData[i];
        }
    }

    return oss.str();
}

bool ProtocolProcessor::deserializeResponse(const std::string& rawData, ResponseData& resp) {
    std::istringstream iss(rawData);
    std::string protocolType, successStr, statusCode, message;

    if (!std::getline(iss, protocolType, '|') || protocolType != "RESPONSE") {
        return false;
    }

    if (!std::getline(iss, successStr, '|')) {
        return false;
    }

    std::getline(iss, statusCode, '|');
    std::getline(iss, message, '|');

    bool success = (successStr == "SUCCESS");
    ResponseData tempResp(success, statusCode, message);

    // 解析额外数据（如果有）
    std::string additionalStr;
    if (std::getline(iss, additionalStr)) {
        std::istringstream dataStream(additionalStr);
        std::string item;
        while (std::getline(dataStream, item, ',')) {
            if (!item.empty()) {
                tempResp.additionalData.push_back(item);
            }
        }
    }

    resp = tempResp;
    return true;
}

Type ProtocolProcessor::parseProtocolType(const std::string& data) {
    size_t pos = data.find('|');
    std::string typeStr = data.substr(0, pos);

    if (typeStr == "MESSAGE") return Message;
    else if (typeStr == "LOGIN") return Login;
    else if (typeStr == "LOGOUT") return Logout;
    else if (typeStr == "RESPONSE") return Response;
    else if (typeStr == "HEARTBEAT") return Heartbeat;
    else if (typeStr == "ACK") return Ack;
    else return static_cast<Type>(-1); // 无效类型
}

bool ProtocolProcessor::validateMessageFields(const MessageData& msg) {
    return !msg.senderId.empty() &&
           !msg.receiverId.empty() &&
           !msg.content.empty() &&
           msg.content.length() <= 1000; // 消息长度限制
}

std::string ProtocolProcessor::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// ACK序列化
std::string ProtocolProcessor::serializeAck(const AckData& ack) {
    std::ostringstream oss;
    oss << "ACK|"
        << ack.messageId << "|"
        << ack.receiverId << "|"
        << (ack.timestamp.empty() ? getCurrentTimestamp() : ack.timestamp);
    return oss.str();
}

// ACK反序列化
bool ProtocolProcessor::deserializeAck(const std::string& rawData, AckData& ack) {
    std::istringstream iss(rawData);
    std::string protocolType;

    if (!std::getline(iss, protocolType, '|') || protocolType != "ACK") {
        return false;
    }

    std::string messageId, receiverId, timestamp;
    if (!std::getline(iss, messageId, '|') ||
        !std::getline(iss, receiverId, '|') ||
        !std::getline(iss, timestamp, '|')) {
        return false;
    }

    if (messageId.empty() || receiverId.empty()) {
        return false;
    }

    ack = AckData(messageId, receiverId, timestamp);
    return true;
}

// 生成唯一消息ID
std::string ProtocolProcessor::generateMessageId() {
    static std::atomic<size_t> counter{0};
    std::stringstream ss;
    ss << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count() << "_" << ++counter;
    return ss.str();
}

// 协议类的实现
void LoginProtocol::process() const {
    std::cout << "[LoginProtocol] Processing login request" << std::endl;
}

void LogoutProtocol::process() const {
    std::cout << "[LogoutProtocol] Processing logout request" << std::endl;
}

void MessageProtocol::process() const {
    std::cout << "[MessageProtocol] Processing message transmission" << std::endl;
}

void ResponseProtocol::process() const {
    std::cout << "[ResponseProtocol] Processing response transmission" << std::endl;
}

void AckProtocol::process() const {
    std::cout << "[AckProtocol] Processing ACK transmission" << std::endl;
}
