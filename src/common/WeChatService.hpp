#pragma once
#include "Service.h"
#include <iostream>

class WeChatService : public Service {
private:
    void* m_platform; // 存储平台引用

public:
    WeChatService(){ m_name = "WeChat"; m_platform = nullptr; }

    void attachPlatform(void* platform) override {
        m_platform = platform;
    }

    bool login(const std::string& uid, const std::string& password) override {
        (void)password; m_userId = uid; m_loggedIn = true; return true;
    }

    void groupFeatureDemo() override {
        std::cout << "[WeChat] 特色：仅邀请入群，不支持临时讨论组，仅群主特权\n";
    }

    bool sendMessage(const std::string& toUserId, const std::string& text) override {
        std::cout << "[WeChat] " << m_userId << " -> " << toUserId << ": " << text << "\n";
        return true;
    }
};
