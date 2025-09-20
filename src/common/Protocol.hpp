#include <iostream>
#include <memory>
#include <vector>
#include <sstream>
#include <string>

// 枚举类型定义
enum Type { Login, Logout, Message, Response, Heartbeat, Ack};

// 消息类型字符串常量
// 新增：统一的消息类型标识符

// 消息结构体定义
struct MessageData {
    std::string messageId;  // 新增：消息唯一标识
    std::string senderId;
    std::string receiverId;
    std::string content;
    std::string timestamp;

    MessageData() = default;
    MessageData(std::string sender, std::string receiver, std::string msg)
        : senderId(sender), receiverId(receiver), content(msg) {}
    MessageData(std::string msgId, std::string sender, std::string receiver, std::string msg)
        : messageId(msgId), senderId(sender), receiverId(receiver), content(msg) {}
};

struct ResponseData {
    bool success;
    std::string statusCode;
    std::string message;
    std::vector<std::string> additionalData;

    ResponseData() : success(false) {}
    ResponseData(bool ok, std::string code, std::string msg)
        : success(ok), statusCode(code), message(msg) {}
};

// 新增：ACK数据结构体
struct AckData {
    std::string messageId;      // 被确认的消息ID
    std::string receiverId;     // 发送ACK的接收者ID
    std::string timestamp;      // ACK发送时间戳

    AckData() = default;
    AckData(std::string msgId, std::string receiver, std::string ts = "")
        : messageId(msgId), receiverId(receiver), timestamp(ts) {}
};

// 基类Protocol
class Protocol {
protected:
    Type type;  // 保护成员，允许子类访问
    Protocol(Type t) : type(t) {}  // 保护构造函数，只能通过子类实例化
public:
    virtual ~Protocol() = default;  // 虚析构函数，确保子类正确析构
    Type getType() const { return type; }
    virtual void process() const = 0;  // 纯虚函数，定义接口
};

// 具体产品：登录协议
class LoginProtocol : public Protocol {
public:
    LoginProtocol() : Protocol(Login) {}
    void process() const override ;
};

// 具体产品：登出协议
class LogoutProtocol : public Protocol {
public:
    LogoutProtocol() : Protocol(Logout) {}
    void process() const override ;
};

// 具体产品：消息协议
class MessageProtocol : public Protocol {
public:
    MessageProtocol() : Protocol(Message) {}
    void process() const override;
};

class ResponseProtocol : public Protocol{
public:
    ResponseProtocol() : Protocol(Response) {}
    void process() const override;
};

// 新增：ACK协议
class AckProtocol : public Protocol {
public:
    AckProtocol() : Protocol(Ack) {}
    void process() const override;
};

// 新增：协议处理器类 - 负责消息序列化/反序列化
class ProtocolProcessor {
public:
    // 消息序列化：MessageData -> 字符串
    static std::string serializeMessage(const MessageData& msg);
    // 消息反序列化：字符串 -> MessageData
    static bool deserializeMessage(const std::string& rawData, MessageData& msg);

    // 响应序列化
    static std::string serializeResponse(const ResponseData& resp);
    // 响应反序列化
    static bool deserializeResponse(const std::string& rawData, ResponseData& resp);

    // ACK序列化
    static std::string serializeAck(const AckData& ack);
    // ACK反序列化
    static bool deserializeAck(const std::string& rawData, AckData& ack);

    // 消息ID生成
    static std::string generateMessageId();

    // 协议类型判断
    static Type parseProtocolType(const std::string& data);

private:
    // 数据验证
    static bool validateMessageFields(const MessageData& msg);
    static std::string getCurrentTimestamp();
};


// 工厂类：负责创建Protocol对象
class ProtocolFactory {
public:
    // 工厂方法：根据类型创建对应的Protocol对象
    // 使用智能指针自动管理内存
    static std::unique_ptr<Protocol> createProtocol(Type type) {
        switch (type) {
            case Login:
                return std::unique_ptr<Protocol>(new LoginProtocol());
            case Logout:
                return std::unique_ptr<Protocol>(new LogoutProtocol());
            case Message:
                return std::unique_ptr<Protocol>(new MessageProtocol());
            case Response:
                return std::unique_ptr<Protocol>(new ResponseProtocol());
            case Ack:
                return std::unique_ptr<Protocol>(new AckProtocol());
            default:
                throw std::invalid_argument("无效的协议类型");
        }
    }
};
