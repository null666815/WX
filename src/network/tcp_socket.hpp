#ifndef TCP_SOCKET_HPP
#define TCP_SOCKET_HPP

#include <string>
#include <cstdint>
#include <mutex>
#include <atomic>

#ifdef __linux__
// 添加poll支持用于非阻塞accept
extern "C" {
#include <sys/poll.h>
}
#endif

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  include <basetsd.h>
   typedef SOCKET SocketHandle;
   typedef int socklen_t;
   typedef SSIZE_T ssize_t; // Windows 上补上 ssize_t 类型
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <cstring>
#  include <cerrno>
#  include <signal.h>
#  if defined(__linux__)
#    include <sys/poll.h> // Linux poll支持
#  endif
   typedef int SocketHandle;
#endif

class TcpSocket {
public:
    TcpSocket();
    ~TcpSocket();

    // Windows 特有初始化/清理
    bool init();
    void cleanup();

    // 基本 socket 操作
    bool create();
    bool bind(uint16_t port, const std::string& ip = "0.0.0.0");
    bool listen(int backlog = 5);
    SocketHandle accept(std::string& clientIp, uint16_t& clientPort);

    // 非阻塞accept with timeout (用于Linux兼容性)
    bool setListenNonBlocking(bool enable);
    SocketHandle acceptNonBlocking(std::string& clientIp, uint16_t& clientPort, int timeoutMs);

    bool connect(const std::string& ip, uint16_t port);

    // 低级 send/recv（会保障全部发送/部分读取）
    ssize_t send(const std::string& data);
    ssize_t recv(std::string& data, size_t maxLen = 4096);

    // 高级消息管道：4 字节长度前缀（网络字节序）
    bool sendPipeMessage(const std::string& message);
    bool receivePipeMessage(std::string& message, uint32_t timeoutSec = 5);

    // 超时 / 非阻塞 控制
    void setReceiveTimeout(int seconds);
    bool setNonBlockingMode(bool enable);

    // 状态/工具
    void close();
    void setHandle(SocketHandle handle);
    bool isSocketValid() const;
    bool isConnected() const;
    std::string getLastError() const;
    int getLastErrorCode() const;

    // 允许移动操作
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

private:
    // 禁止复制，允许移动
    TcpSocket(const TcpSocket& other) = delete;
    TcpSocket& operator=(const TcpSocket& other) = delete;

    // 内部close函数：假设调用者已持有锁，无需再次锁定
    void close(bool alreadyLocked);

private:
    SocketHandle m_socket;
    std::atomic<bool> m_socketValid;
    mutable std::mutex m_socketMutex;

    std::string m_lastError;
    int m_lastErrorCode;

    void setSocketValid(bool v);
    std::string errorToString(int code) const;

    // helper: 完全发送/接收
    bool sendAll(const char* buf, size_t len);
    bool recvAll(char* buf, size_t len);
};

#endif // TCP_SOCKET_HPP
