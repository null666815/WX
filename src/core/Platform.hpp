#pragma once
#include <unordered_map>
#include <memory>
#include <set>
#include <vector>
#include <functional>
#include "User.hpp"
#include "Group.hpp"
#include "../common/Service.h"
#include "../common/Repository.hpp"

class Platform {
public:
    // 用户与群的全局存储
    std::unordered_map<std::string, User> users;     // id -> User
    std::unordered_map<std::string, Group> groups;   // groupNo -> Group

    // 服务实例索引：key=(serviceName+"|"+userId)
    std::unordered_map<std::string, std::unique_ptr<Service>> servicesIndex;

    // 工具
    static std::string key(const std::string& s, const std::string& uid){ return s+"|"+uid; }

    // 开通服务（实例化服务并挂载平台）
    template<class Svc, class...Args>
    Service* openService(const std::string& userId, Args&&...args){
        auto svc = std::unique_ptr<Service>(new Svc(std::forward<Args>(args)...));
        svc->attachPlatform(this);
        std::string k = key(svc->name(), userId);
        Service* raw = svc.get();
        servicesIndex[k] = std::move(svc);
        return raw;
    }

    // 登录同步：某服务登录成功后，把该用户开通的其它服务置为开通/已验证（演示用）
    void autoLoginSync(const std::string& userId){
        // 在真实实现中可记录 token；这里仅打印演示
        (void)userId;
    }

    // 共同好友
    std::vector<std::string> mutualFriends(const std::string& u1, const std::string& u2) const{
        std::vector<std::string> res;
        auto it1 = users.find(u1), it2 = users.find(u2);
        if(it1==users.end()||it2==users.end()) return res;
        const auto& A = it1->second.friends();
        const auto& B = it2->second.friends();
        for(const auto& x: A) if(B.count(x)) res.push_back(x);
        return res;
    }

    // 查找服务
    Service* getService(const std::string& serviceName, const std::string& userId) {
        std::string k = key(serviceName, userId);
        auto it = servicesIndex.find(k);
        if (it == servicesIndex.end()) return nullptr;
        return it->second.get();
    }

    // 文件 I/O
    bool load(const std::string& userPath, const std::string& groupPath) {
        return Repository::loadUsers(userPath, users) && Repository::loadGroups(groupPath, groups);
    }
    bool save(const std::string& userPath, const std::string& groupPath) {
        return Repository::saveUsers(userPath, users) && Repository::saveGroups(groupPath, groups);
    }
};
