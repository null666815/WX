#pragma once
#include <string>
#include <unordered_set>
#include <unordered_map>

enum class GroupType { QQ, WeChat };

class Group {
public:
    Group() = default;
    Group(std::string no, GroupType t) : m_groupNo(std::move(no)), m_type(t) {}

    const std::string& number() const { return m_groupNo; }
    GroupType type() const { return m_type; }

    void setOwner(const std::string& uid) { m_ownerId = uid; }
    const std::string& owner() const { return m_ownerId; }

    void addAdmin(const std::string& uid) { m_adminIds.insert(uid); }
    void removeAdmin(const std::string& uid) { m_adminIds.erase(uid); }
    const std::unordered_set<std::string>& admins() const { return m_adminIds; }

    bool addMember(const std::string& uid) {
        return m_memberIds.insert(uid).second;
    }
    void removeMember(const std::string& uid) { m_memberIds.erase(uid); }
    const std::unordered_set<std::string>& members() const { return m_memberIds; }

    // ===== 群管理特色策略 =====
    // QQ：可申请加入；WeChat：仅邀请加入
    bool canApplyJoin() const { return m_type == GroupType::QQ; }
    bool canInviteOnly() const { return m_type == GroupType::WeChat; }

    // QQ：允许临时讨论组；WX：不允许（在演示时打印差异）
    bool allowTempSubgroup() const { return m_type == GroupType::QQ; }

private:
    std::string m_groupNo;           // 1001/1002/...
    GroupType m_type{GroupType::QQ};
    std::string m_ownerId;
    std::unordered_set<std::string> m_adminIds;
    std::unordered_set<std::string> m_memberIds;
};
