#pragma once
#include <string>

class Service {
protected:
    std::string m_name;
    std::string m_userId;
    bool m_loggedIn = false;

public:
    virtual ~Service() = default;

    std::string name() const { return m_name; }
    std::string userId() const { return m_userId; }
    bool loggedIn() const { return m_loggedIn; }

    virtual void attachPlatform(void* platform) = 0;

    virtual bool login(const std::string& uid, const std::string& password) = 0;
    virtual void groupFeatureDemo() = 0;
    virtual bool sendMessage(const std::string& toUserId, const std::string& text) = 0;

protected:
    virtual void onAttachPlatform() {}
};
