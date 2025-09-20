#pragma once

#include <string>
#include <chrono>
#include <ctime>

struct Message {
    std::string fromId;    // 发送者ID
    std::string toId;      // 接收者ID（可以是用户ID或群组ID）
    std::string content;   // 消息内容
    std::chrono::system_clock::time_point timestamp; // 发送时间

    Message(std::string from, std::string to, std::string msg)
        : fromId(std::move(from)), toId(std::move(to)), content(std::move(msg)) {
        timestamp = std::chrono::system_clock::now();
    }

    // 获取格式化的时间字符串
    std::string getFormattedTime() const {
        auto t = std::chrono::system_clock::to_time_t(timestamp);
        std::tm tm = *std::localtime(&t);
        char buffer[20];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
        return std::string(buffer);
    }
};
