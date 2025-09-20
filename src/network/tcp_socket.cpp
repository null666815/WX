#include "tcp_socket.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cstring>

// 控制单条消息最大长度（可调整）
static const uint32_t MAX_PIPE_MESSAGE_SIZE = 64 * 1024; // 64KB

TcpSocket::TcpSocket()
    : m_socket(
#ifdef _WIN32
        INVALID_SOCKET
#else
        -1
#endif
      ),
      m_socketValid(false),
      m_lastError(),
      m_lastErrorCode(0) {}

TcpSocket::~TcpSocket() {
    close();
}

bool TcpSocket::init() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        m_lastError = "WSAStartup failed";
        return false;
    }
#else
    // 忽略 SIGPIPE，避免写断开的 socket 导致进程退出
    signal(SIGPIPE, SIG_IGN);
#endif
    return true;
}

void TcpSocket::cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

bool TcpSocket::create() {
    std::lock_guard<std::mutex> lk(m_socketMutex);
    close(true); // 已持有锁，直接调用内部版本

    m_socket = ::socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
    if (m_socket == INVALID_SOCKET) {
        m_lastErrorCode = WSAGetLastError();
        m_lastError = errorToString(m_lastErrorCode);
        setSocketValid(false);
        std::cerr << "[DEBUG] Socket creation failed on Windows: " << m_lastError << " (code: " << m_lastErrorCode << ")" << std::endl;
        return false;
    }
#else
    if (m_socket < 0) {
        m_lastErrorCode = errno;
        m_lastError = errorToString(m_lastErrorCode);
        setSocketValid(false);
        std::cerr << "[DEBUG] Socket creation failed on Linux: " << m_lastError << " (errno: " << m_lastErrorCode << ")" << std::endl;
        return false;
    }
#endif
    std::cout << "[DEBUG] Socket created successfully, socket fd: " << m_socket << std::endl;

    // SO_REUSEADDR (+ optionally SO_REUSEPORT)
    int opt = 1;
    int setsockopt_result;
    setsockopt_result = setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    if (setsockopt_result < 0) {
        m_lastErrorCode = errno;
        m_lastError = errorToString(m_lastErrorCode);
        std::cerr << "[DEBUG] SO_REUSEADDR failed on Linux: " << m_lastError << " (errno: " << m_lastErrorCode << ")" << std::endl;
        close();
        return false;
    }
    std::cout << "[DEBUG] SO_REUSEADDR set successfully" << std::endl;

    setSocketValid(true);
    return true;
}

bool TcpSocket::bind(uint16_t port, const std::string& ip) {
    if (!isSocketValid()) { m_lastError = "socket not created"; return false; }

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

#ifdef _WIN32
    if (ip == "0.0.0.0") {
        addr.sin_addr.S_un.S_addr = INADDR_ANY;
    } else {
        if (InetPtonA(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
            addr.sin_addr.S_un.S_addr = inet_addr(ip.c_str());
        }
    }
#else
    if (ip == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
            addr.sin_addr.s_addr = inet_addr(ip.c_str());
        }
    }
#endif

    if (::bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
#ifdef _WIN32
        m_lastErrorCode = WSAGetLastError();
#else
        m_lastErrorCode = errno;
#endif
        m_lastError = errorToString(m_lastErrorCode);
        return false;
    }
    return true;
}

bool TcpSocket::listen(int backlog) {
    if (!isSocketValid()) { m_lastError = "socket not created"; return false; }
    if (::listen(m_socket, backlog) < 0) {
#ifdef _WIN32
        m_lastErrorCode = WSAGetLastError();
#else
        m_lastErrorCode = errno;
#endif
        m_lastError = errorToString(m_lastErrorCode);
        return false;
    }
    return true;
}

SocketHandle TcpSocket::accept(std::string& clientIp, uint16_t& clientPort) {
    if (!isSocketValid()) { m_lastError = "socket not created"; return -1; }

    sockaddr_in cli{};
    socklen_t len = sizeof(cli);
    SocketHandle cliSock = ::accept(m_socket, reinterpret_cast<sockaddr*>(&cli), &len);
#ifdef _WIN32
    if (cliSock == INVALID_SOCKET) {
        m_lastErrorCode = WSAGetLastError();
        m_lastError = errorToString(m_lastErrorCode);
        return -1;
    }
#else
    if (cliSock < 0) {
        m_lastErrorCode = errno;
        m_lastError = errorToString(m_lastErrorCode);
        return -1;
    }
#endif

    char ipbuf[INET_ADDRSTRLEN] = {0};
#ifdef _WIN32
    InetNtopA(AF_INET, &cli.sin_addr, ipbuf, sizeof(ipbuf));
#else
    inet_ntop(AF_INET, &cli.sin_addr, ipbuf, sizeof(ipbuf));
#endif
    clientIp = ipbuf;
    clientPort = ntohs(cli.sin_port);
    return cliSock;
}

// 非阻塞模式设置
bool TcpSocket::setListenNonBlocking(bool enable) {
    if (!isSocketValid()) { m_lastError = "socket not created"; return false; }
    return setNonBlockingMode(enable);
}

// 非阻塞accept with timeout
SocketHandle TcpSocket::acceptNonBlocking(std::string& clientIp, uint16_t& clientPort, int timeoutMs) {
#ifdef __linux__
    if (!isSocketValid()) { m_lastError = "socket not created"; return -1; }

    // 使用poll等待连接可读
    pollfd pfd;
    pfd.fd = m_socket;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ret = poll(&pfd, 1, timeoutMs);
    if (ret < 0) {
        m_lastErrorCode = errno;
        m_lastError = errorToString(m_lastErrorCode);
        return -1;
    }

    if (ret == 0) {
        m_lastError = "timeout";
        return -1; // timeout
    }

    if (!(pfd.revents & POLLIN)) {
        m_lastError = "no data";
        return -1;
    }

    // 连接可用，执行accept
    sockaddr_in cli{};
    socklen_t len = sizeof(cli);
    SocketHandle cliSock = ::accept(m_socket, reinterpret_cast<sockaddr*>(&cli), &len);

    if (cliSock < 0) {
        m_lastErrorCode = errno;
        m_lastError = errorToString(m_lastErrorCode);
        return -1;
    }

    char ipbuf[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &cli.sin_addr, ipbuf, sizeof(ipbuf));
    clientIp = ipbuf;
    clientPort = ntohs(cli.sin_port);
    return cliSock;

#else // Windows或其他系统，回退到阻塞accept
    return accept(clientIp, clientPort);
#endif
}

bool TcpSocket::connect(const std::string& ip, uint16_t port) {
    if (!isSocketValid()) {
        if (!create()) return false;
    }

    sockaddr_in svr{};
    std::memset(&svr, 0, sizeof(svr));
    svr.sin_family = AF_INET;
    svr.sin_port = htons(port);
#ifdef _WIN32
    if (InetPtonA(AF_INET, ip.c_str(), &svr.sin_addr) != 1) {
        svr.sin_addr.S_un.S_addr = inet_addr(ip.c_str());
    }
#else
    if (inet_pton(AF_INET, ip.c_str(), &svr.sin_addr) != 1) {
        svr.sin_addr.s_addr = inet_addr(ip.c_str());
    }
#endif

    if (::connect(m_socket, reinterpret_cast<sockaddr*>(&svr), sizeof(svr)) < 0) {
#ifdef _WIN32
        m_lastErrorCode = WSAGetLastError();
#else
        m_lastErrorCode = errno;
#endif
        m_lastError = errorToString(m_lastErrorCode);
        return false;
    }
    return true;
}

// helper: 完整发送
bool TcpSocket::sendAll(const char* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int n = ::send(m_socket, buf + sent, static_cast<int>(len - sent), 0);
#else
        ssize_t n = ::send(m_socket, buf + sent, len - sent, MSG_NOSIGNAL);
#endif
        if (n > 0) { sent += static_cast<size_t>(n); continue; }
        if (n == 0) { m_lastError = "peer closed"; return false; }

#ifdef _WIN32
        int err = WSAGetLastError();
        if (err == WSAEINTR) continue;
        m_lastErrorCode = err;
#else
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        m_lastErrorCode = errno;
#endif
        m_lastError = errorToString(m_lastErrorCode);
        return false;
    }
    return true;
}

// helper: 完整接收指定长度数据
bool TcpSocket::recvAll(char* buf, size_t len) {
    size_t got = 0;
    while (got < len) {
#ifdef _WIN32
        int r = ::recv(m_socket, buf + got, static_cast<int>(len - got), 0);
#else
        ssize_t r = ::recv(m_socket, buf + got, len - got, 0);
#endif
        if (r > 0) { got += static_cast<size_t>(r); continue; }
        if (r == 0) { m_lastError = "peer closed"; return false; }
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err == WSAEINTR) continue;
        if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) { m_lastError = "no data"; return false; }
        m_lastErrorCode = err;
#else
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) { m_lastError = "no data"; return false; }
        m_lastErrorCode = errno;
#endif
        m_lastError = errorToString(m_lastErrorCode);
        return false;
    }
    return true;
}

// 直接send包装（会尝试一次完整发送）
ssize_t TcpSocket::send(const std::string& data) {
    if (!isSocketValid()) { m_lastError = "invalid socket"; return -1; }
    if (data.empty()) return 0;
    if (!sendAll(data.data(), data.size())) return -1;
    return static_cast<ssize_t>(data.size());
}

// recv：一次性读取可用数据（最多 maxLen）
ssize_t TcpSocket::recv(std::string& data, size_t maxLen) {
    if (!isSocketValid()) { m_lastError = "invalid socket"; return -1; }
    std::vector<char> buf(maxLen);
#ifdef _WIN32
    int r = ::recv(m_socket, buf.data(), static_cast<int>(maxLen), 0);
#else
    ssize_t r = ::recv(m_socket, buf.data(), maxLen, 0);
#endif
    if (r > 0) {
        data.assign(buf.data(), static_cast<size_t>(r));
        return r;
    }
    if (r == 0) { data.clear(); return 0; } // orderly shutdown
#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) { m_lastError = "no data"; return -1; }
    m_lastErrorCode = err;
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) { m_lastError = "no data"; return -1; }
    m_lastErrorCode = errno;
#endif
    m_lastError = errorToString(m_lastErrorCode);
    return -1;
}

// 长度前缀消息发送（4 字节网络序）
bool TcpSocket::sendPipeMessage(const std::string& message) {
    if (!isSocketValid()) { m_lastError = "invalid socket"; return false; }
    if (message.size() > MAX_PIPE_MESSAGE_SIZE) { m_lastError = "message too large"; return false; }
    uint32_t len = static_cast<uint32_t>(message.size());
    uint32_t netlen = htonl(len);
    if (!sendAll(reinterpret_cast<const char*>(&netlen), sizeof(netlen))) return false;
    if (len > 0) {
        if (!sendAll(message.data(), len)) return false;
    }
    return true;
}

// receivePipeMessage: timeoutSec == 0 => 非阻塞立刻返回； >0 => setsockopt 超时（秒）
bool TcpSocket::receivePipeMessage(std::string& message, uint32_t timeoutSec) {
    message.clear();
    if (!isSocketValid()) { m_lastError = "invalid socket"; return false; }
    // set receive timeout
    setReceiveTimeout(static_cast<int>(timeoutSec));

    // peek 4 字节长度
    uint32_t netlen = 0;
#ifdef _WIN32
    int peekR = ::recv(m_socket, reinterpret_cast<char*>(&netlen), sizeof(netlen), MSG_PEEK);
#else
    ssize_t peekR = ::recv(m_socket, reinterpret_cast<char*>(&netlen), sizeof(netlen), MSG_PEEK);
#endif
    if (peekR <= 0) {
        // no data or error or closed
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) { m_lastError = "no data"; return false; }
        m_lastErrorCode = err;
#else
        if (peekR == 0) { m_lastError = "peer closed"; return false; }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) { m_lastError = "no data"; return false; }
        m_lastErrorCode = errno;
#endif
        m_lastError = errorToString(m_lastErrorCode);
        return false;
    }

    // 如果 peekR < 4，说明没有完整长度头：作为兼容性 fallback，直接读取一次 available 数据
    if (static_cast<size_t>(peekR) < sizeof(netlen)) {
        std::string tmp;
        ssize_t r = recv(tmp, MAX_PIPE_MESSAGE_SIZE);
        if (r > 0) { message = tmp; return true; }
        return false;
    }

    // 读取并消费长度字段
    if (!recvAll(reinterpret_cast<char*>(&netlen), sizeof(netlen))) return false;
    uint32_t payloadLen = ntohl(netlen);
    if (payloadLen == 0) { message.clear(); return true; }
    if (payloadLen > MAX_PIPE_MESSAGE_SIZE) { m_lastError = "payload too large"; return false; }

    std::vector<char> buf(payloadLen);
    if (!recvAll(buf.data(), payloadLen)) return false;
    message.assign(buf.data(), payloadLen);
    return true;
}

void TcpSocket::close() {
    std::lock_guard<std::mutex> lk(m_socketMutex);
    close(true); // 已持有锁，直接调用内部版本
}

// 内部close函数：假设调用者已持有锁，无需再次锁定
void TcpSocket::close(bool alreadyLocked) {
#ifdef _WIN32
    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
#else
    if (m_socket >= 0) {
        ::close(m_socket);
        m_socket = -1;
    }
#endif
    setSocketValid(false);
}

void TcpSocket::setHandle(SocketHandle handle) {
    std::lock_guard<std::mutex> lk(m_socketMutex);
    // 关闭原 socket
    if (isSocketValid()) {
#ifdef _WIN32
        closesocket(m_socket);
#else
        ::close(m_socket);
#endif
    }
    m_socket = handle;
    setSocketValid(handle >= 0
#ifdef _WIN32
                   && handle != INVALID_SOCKET
#endif
    );
    m_lastError.clear();
    m_lastErrorCode = 0;
}

void TcpSocket::setReceiveTimeout(int seconds) {
    if (!isSocketValid()) return;
#ifdef _WIN32
    DWORD ms = static_cast<DWORD>(seconds * 1000);
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif
}

bool TcpSocket::setNonBlockingMode(bool enable) {
    if (!isSocketValid()) { m_lastError = "invalid socket"; return false; }
#ifdef _WIN32
    u_long mode = enable ? 1 : 0;
    if (ioctlsocket(m_socket, FIONBIO, &mode) != 0) {
        m_lastErrorCode = WSAGetLastError();
        m_lastError = errorToString(m_lastErrorCode);
        return false;
    }
#else
    int flags = fcntl(m_socket, F_GETFL, 0);
    if (flags == -1) { m_lastErrorCode = errno; m_lastError = errorToString(m_lastErrorCode); return false; }
    int newf = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(m_socket, F_SETFL, newf) == -1) {
        m_lastErrorCode = errno; m_lastError = errorToString(m_lastErrorCode); return false;
    }
#endif
    return true;
}

bool TcpSocket::isSocketValid() const {
#ifdef _WIN32
    return m_socket != INVALID_SOCKET && m_socketValid.load();
#else
    return m_socket >= 0 && m_socketValid.load();
#endif
}

void TcpSocket::setSocketValid(bool v) {
    m_socketValid.store(v);
}

bool TcpSocket::isConnected() const {
    if (!isSocketValid()) return false;
    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(m_socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) < 0) return false;
    return (err == 0);
}

std::string TcpSocket::getLastError() const {
    return m_lastError;
}

int TcpSocket::getLastErrorCode() const {
    return m_lastErrorCode;
}

std::string TcpSocket::errorToString(int code) const {
#ifdef _WIN32
    LPSTR msgBuf = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, static_cast<DWORD>(code), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   reinterpret_cast<LPSTR>(&msgBuf), 0, nullptr);
    std::string s;
    if (msgBuf) {
        s = msgBuf;
        LocalFree(msgBuf);
    } else {
        s = "Unknown error";
    }
    return s;
#else
    return std::string(strerror(code));
#endif
}

// move ctor
TcpSocket::TcpSocket(TcpSocket&& other) noexcept {
    std::lock_guard<std::mutex> lk(other.m_socketMutex);
    m_socket = other.m_socket;
    m_socketValid.store(other.m_socketValid.load());
    m_lastError = std::move(other.m_lastError);
    m_lastErrorCode = other.m_lastErrorCode;
#ifdef _WIN32
    other.m_socket = INVALID_SOCKET;
#else
    other.m_socket = -1;
#endif
    other.setSocketValid(false);
}

// move assign
TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        close();
        std::lock_guard<std::mutex> lk(other.m_socketMutex);
        m_socket = other.m_socket;
        m_socketValid.store(other.m_socketValid.load());
        m_lastError = std::move(other.m_lastError);
        m_lastErrorCode = other.m_lastErrorCode;
#ifdef _WIN32
        other.m_socket = INVALID_SOCKET;
#else
        other.m_socket = -1;
#endif
        other.setSocketValid(false);
    }
    return *this;
}
