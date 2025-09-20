#pragma once
#include <string>
#include <unordered_set>

struct Birthday {
    int year{}, month{}, day{};
};

class User {
public:
    User() = default;
    User(std::string id, std::string nick) : m_id(std::move(id)), m_nickname(std::move(nick)) {}

    const std::string& id() const { return m_id; }
    const std::string& nickname() const { return m_nickname; }
    void setNickname(const std::string& n) { m_nickname = n; }

    const std::string& location() const { return m_location; }
    void setLocation(const std::string& loc) { m_location = loc; }

    const Birthday& birthday() const { return m_birthday; }
    void setBirthday(Birthday b) { m_birthday = b; }

    void addFriend(const std::string& uid) { m_friendIds.insert(uid); }
    void removeFriend(const std::string& uid) { m_friendIds.erase(uid); }
    const std::unordered_set<std::string>& friends() const { return m_friendIds; }

    void joinGroup(const std::string& gid) { m_groupIds.insert(gid); }
    void leaveGroup(const std::string& gid) { m_groupIds.erase(gid); }
    const std::unordered_set<std::string>& groups() const { return m_groupIds; }

private:
    std::string m_id;
    std::string m_nickname;
    Birthday m_birthday{};
    std::string m_location;
    std::unordered_set<std::string> m_friendIds;
    std::unordered_set<std::string> m_groupIds;
};
