#pragma once

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#endif

#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <stddef.h>
#include <memory>
#include <cstddef>

#include "ChatServer.hpp"

// 客户端连接处理器类
class ClientHandler
{
private:
    std::string clientIp;
    uint16_t clientPort;
    ChatServer &chatServer;
    std::atomic<bool> &serverRunning;
    void* session = nullptr; // 管理的会话指针，ChatServer负责所有socket操作

public:
    ClientHandler(TcpSocket &&socketRef, const std::string &ip, uint16_t port,
                  ChatServer &server, std::atomic<bool> &running)
        : clientIp(ip), clientPort(port), chatServer(server), serverRunning(running)
    {
        std::cout << "[ClientHandler] Created for client " << clientIp << ":" << clientPort << std::endl;

        // 直接创建会话，ChatServer 管理 socket
        session = reinterpret_cast<void*>(chatServer.createSession(ip, port, std::move(socketRef)));
        if (session)
        {
            std::cout << "[ClientHandler] ✅ ClientSession created successfully, ChatServer now manages socket" << std::endl;
        }
        else
        {
            std::cout << "[ClientHandler] ❌ ClientSession creation failed" << std::endl;
        }
    }

    ClientHandler(ClientHandler &&other) noexcept = default;

    void operator()()
    {
        try
        {
            std::cout << "[Server] Started handling client: " << clientIp << ":" << clientPort << std::endl;

            // 性能优化：预分配字符串，减少内存分配
            std::string message;
            message.reserve(1024); // 预分配1KB空间

            bool clientActive = true;
            while (clientActive && serverRunning)
            {
                message.clear();

                // 通过ChatServer代理接收消息
                bool hasMessage = chatServer.receiveFromClient(session, message, clientActive);

                if (!hasMessage)
                {
                    // ChatServer已经处理了连接状态检查，这里只需要等待
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                if (!message.empty())
                {
                    // 优化日志输出：减少冗余分隔符，合并相关信息
                    std::cout << "[Server] RECEIVED from " << clientIp << ":" << clientPort
                              << " [" << message.length() << " bytes]: '" << message << "'" << std::endl;

                    try
                    {
                        // 快速路径优化：先检查是否是管道消息
                        if (message.find("|") != std::string::npos)
                        {
                            // ChatServer 处理消息
                            std::string response = chatServer.processMessage(message, clientIp + ":" + std::to_string(clientPort));
                            chatServer.sendToClient(session, response);
                        }
                        else if (message.length() > 0)
                        {
                            // 对于非管道消息，发送通用成功响应
                            chatServer.sendToClient(session, "RESPONSE|SUCCESS|MESSAGE_RECEIVED");
                        }
                        // 忽略空消息，不输出日志
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << "[Server] Exception from " << clientIp << ":" << clientPort
                                  << ": " << e.what() << std::endl;
                        chatServer.sendToClient(session, "RESPONSE|ERROR|Processing failed: " + std::string(e.what()));
                    }
                    catch (...)
                    {
                        std::cerr << "[Server] Unknown exception from " << clientIp << ":" << clientPort << std::endl;
                        chatServer.sendToClient(session, "RESPONSE|ERROR|Unknown processing error");
                    }
                }
            }

            std::cout << "[Server] Finished handling client: " << clientIp << ":" << clientPort << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Server] Exception in client handler for " << clientIp << ":" << clientPort
                      << ": " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "[Server] Unknown exception in client handler for " << clientIp << ":" << clientPort << std::endl;
        }
    }

    ~ClientHandler()
    {
        // 清理资源 - ChatServer 自动管理会话生命周期
    }
};
