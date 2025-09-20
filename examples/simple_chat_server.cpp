#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <stddef.h>
#include <memory>
#include <cstddef>
#include "../src/network/tcp_socket.hpp"
#include "../src/core/Platform.hpp"
#include "../src/chat/ChatServer.hpp"
#include "../src/common/Protocol.hpp"


// 使用统一的ClientHandler头文件
#include "../src/chat/ClientHandler.hpp"

// 之前的ClientHandler类已移除 - 使用include/ClientHandler.h
class SimpleChatServer {
private:
    TcpSocket m_serverSocket;
    Platform m_platform;
    ChatServer m_chatServer;
    std::atomic<bool> m_running;
    std::atomic<size_t> m_clientCount;
    std::vector<std::thread> m_clientThreads; // 管理所有客户端线程

public:
    SimpleChatServer()
        : m_chatServer(m_platform), m_running(false), m_clientCount(0) {
        // 加载平台数据
        if (!m_platform.load("data/users.txt", "data/groups.txt")) {
            std::cout << "[Server] Warning: Failed to load platform data files" << std::endl;
        }
    }

    ~SimpleChatServer() {
        stop();
    }

    bool start(uint16_t port) {
        // 初始化服务器套接字
        if (!m_serverSocket.init()) {
            std::cerr << "[Server] Failed to initialize server socket" << std::endl;
            return false;
        }

        if (!m_serverSocket.create()) {
            std::cerr << "[Server] Failed to create server socket" << std::endl;
            m_serverSocket.cleanup();
            return false;
        }

        if (!m_serverSocket.bind(port)) {
            std::cerr << "[Server] Failed to bind to port " << port << std::endl;
            m_serverSocket.close();
            m_serverSocket.cleanup();
            return false;
        }

        if (!m_serverSocket.listen(10)) {
            std::cerr << "[Server] Failed to listen on port " << port << std::endl;
            m_serverSocket.close();
            m_serverSocket.cleanup();
            return false;
        }

        m_running = true;
        std::cout << "[Server] Chat Server started on port " << port << std::endl;
        std::cout << "[Server] Waiting for client connections..." << std::endl;
        std::cout << "[Server] Using C++ std::thread for client handling" << std::endl;

        return true;
    }

    void run() {
        std::cout << "\n=== 服务器运行状态 ===" << std::endl;
        std::cout << "• 使用 C++ std::thread 处理并发客户端" << std::endl;
        std::cout << "• 每个客户端拥有独立的线程" << std::endl;
        std::cout << "• 支持管道协议消息处理" << std::endl;
        std::cout << "• Linux兼容性: 防止accept()阻塞卡死" << std::endl;
        std::cout << "=====================================\n" << std::endl;

        // 设置非阻塞模式（Linux下防止卡死）
        if (!m_serverSocket.setListenNonBlocking(true)) {
            std::cerr << "[Server] Warning: Failed to set non-blocking mode" << std::endl;
        }

        // 主循环：接受新客户端连接
        static int connectionCheckCount = 0;  // 静态计数器，用于减少输出频率
        while (m_running) {
            std::string clientIp;
            uint16_t clientPort;

            // 每10个检查周期（约10秒）输出一次等待信息
            if (++connectionCheckCount >= 10) {
                std::cout << "[Server] Waiting for new client connection (non-blocking)..." << std::endl;
                connectionCheckCount = 0;
            }

#ifdef __linux__
            // Linux: 使用带超时的非阻塞accept
            SocketHandle clientHandle = m_serverSocket.acceptNonBlocking(clientIp, clientPort, 1000); // 1秒超时
#else
            // Windows或其他系统：直接使用阻塞accept
            SocketHandle clientHandle = m_serverSocket.accept(clientIp, clientPort);
#endif

            if (clientHandle == -1) {
                if (!m_running) break;

                // 检查是否是超时（这是正常的，无需打印错误）
                std::string errorMsg = m_serverSocket.getLastError();
                if (errorMsg == "timeout" || errorMsg == "no data") {
                    // Linux下非阻塞accept失败，需要短暂延迟防止CPU占用
                    #ifdef _WIN32
                        Sleep(10);
                    #else
                        usleep(10 * 1000); // 10ms
                    #endif
                    continue; // 静静等待下一个循环
                }

                // 输出错误信息，需要重置计数器确保用户看到
                std::cerr << "[Server] Failed to accept client connection: " << errorMsg << std::endl;
                connectionCheckCount = 0;  // 重置计数器，在下次循环时会立即显示

                // 短暂延时避免CPU占用过高
                #ifdef _WIN32
                    Sleep(10);
                #else
                    usleep(10 * 1000);
                #endif
                continue;
            }

            if (!m_running) break;

            // 创建新的客户套接字
            TcpSocket clientSocket;
            clientSocket.setHandle(clientHandle);

            size_t currentCount = ++m_clientCount;
            std::cout << "[Server] Client #" << currentCount << " connected from "
                      << clientIp << ":" << clientPort << std::endl;

            // 为每个客户端创建独立线程
            try {
                ClientHandler handler(std::move(clientSocket), clientIp, clientPort,
                                    m_chatServer, m_running);
                std::thread clientThread(std::move(handler));

                // 将线程添加到管理列表（用于清理）
                m_clientThreads.push_back(std::move(clientThread));

                // 分离线程，使其独立运行（不用等待结束）
                m_clientThreads.back().detach();

                std::cout << "[Server] Spawned dedicated thread for client #" << currentCount << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[Server] Failed to create thread for client #" << currentCount
                          << ": " << e.what() << std::endl;
            }
        }
    }


public:
    void stop() {
        if (!m_running) return;

        m_running = false;

        std::cout << "\n[Server] Shutting down server..." << std::endl;

        // 等待一段时间让线程自然结束
#ifdef _WIN32
        Sleep(1000);
#else
        usleep(1000 * 1000);
#endif

        // 清理所有线程（已分离，不需要join，但要清理vector）
        m_clientThreads.clear();

        // 关闭服务器套接字
        m_serverSocket.close();
        m_serverSocket.cleanup();

        // 保存平台数据
        if (!m_platform.save("data/users.txt", "data/groups.txt")) {
            std::cout << "[Server] Warning: Failed to save platform data" << std::endl;
        }

        std::cout << "[Server] Server stopped successfully" << std::endl;
    }

    void showStats() {
        std::cout << "\n=== 服务器统计信息 ===" << std::endl;
        std::cout << "已处理客户端数量: " << m_clientCount.load() << std::endl;
        std::cout << "活跃线程数量: " << m_clientThreads.size() << std::endl;
        std::cout << "服务器状态: " << (m_running.load() ? "运行中" : "已停止") << std::endl;
        std::cout << "==========================\n" << std::endl;
    }
};

int main() {
#ifdef _WIN32
    system("chcp 65001");
#endif
    try {
        std::cout << "=== 即时通信服务器 (C++ std::thread版本) ===" << std::endl;
        std::cout << "[Server] Starting with C++ std::thread support" << std::endl;

        SimpleChatServer server;

        if (server.start(8080)) {
            std::cout << "[Server] Server started successfully!" << std::endl;
            std::cout << "[Server] Press Ctrl+C to stop the server..." << std::endl;

            server.run();
        } else {
            std::cerr << "[Server] Failed to start server" << std::endl;
            return -1;
        }

    } catch (const std::exception& e) {
        std::cerr << "[Server] Exception: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
