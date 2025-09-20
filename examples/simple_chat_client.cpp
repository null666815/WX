// ==============================
// 📱 即时通信客户端 - 主入口文件
// ==============================

// ---- System Headers ----
#include <iostream>
#include <string>
#include <iomanip>
#include <limits>

// ---- Project Headers ----
#include "../src/client/ChatClient.hpp"

// ==============================
// 🔧 配置常量
// ==============================

namespace Config
{
    const std::string SERVER_IP   = "127.0.0.1";
    const uint16_t    SERVER_PORT = 8080;
} // namespace Config

// ==============================
// 🚀 主函数 - 应用入口点
// ==============================

/**
 * 主程序入口点
 *
 * 执行流程:
 * 1. 设置系统编码 (Windows)
 * 2. 显示应用信息
 * 3. 初始化ChatClient
 * 4. 交互式用户配置
 * 5. 连接到服务器
 * 6. 启动主应用循环
 * 7. 清理资源并退出
 *
 * @return int 退出码 (0 表示正常退出)
 */
auto main() -> int
{
    // Windows 控制台编码设置
    system("chcp 65001");

    // 显示应用启动信息
    std::cout << "=== 即时通信客户端 ===" << std::endl;
    std::cout << "版本: 1.0.0" << std::endl;
    std::cout << "作者: 您的名字" << std::endl << std::endl;

    // 初始化客户端应用实例
    ChatClientApp client;

    // 平台设置初始化
    std::cout << "初始化平台服务..." << std::endl;
    client.setupPlatform();

    // 交互式用户身份确认
    std::string userId;
    while (userId.empty()) {
        std::cout << "请输入您的用户ID: ";
        std::getline(std::cin >> std::ws, userId);

        if (userId.empty()) {
            std::cout << "用户名不能为空，请重新输入！" << std::endl;
        }
    }

    // 设置用户信息
    client.setUser(userId);
    std::cout << "欢迎使用，" << userId << "！" << std::endl << std::endl;

    // 尝试连接到服务器
    std::cout << "连接到服务器 " << Config::SERVER_IP << ":" << Config::SERVER_PORT << "..." << std::endl;

    const bool connected = client.connect(Config::SERVER_IP, Config::SERVER_PORT);

    if (connected) {
        std::cout << "🎉 连接成功！开始正常通信模式。" << std::endl << std::endl;
    }
    else {
        std::cout << "❌ 连接失败！启动离线演示模式。" << std::endl;
        std::cout << "注意: 在离线模式下，将无法发送或接收消息。" << std::endl << std::endl;
    }

    // 启动主应用循环
    try {
        std::cout << "正在启动聊天系统..." << std::endl;
        client.run();
    }
    catch (const std::exception& e) {
        std::cerr << "❌ 运行时异常: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    // 正常退出
    std::cout << "\n👋 感谢使用即时通信客户端！再见！" << std::endl;
    return EXIT_SUCCESS;
}
