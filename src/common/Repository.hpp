#pragma once
#include <string>
#include <unordered_map>
#include "../core/User.hpp"
#include "../core/Group.hpp"

class Repository {
public:
    static bool loadUsers(const std::string& path, std::unordered_map<std::string, User>& out);
    static bool saveUsers(const std::string& path, const std::unordered_map<std::string, User>& in);

    static bool loadGroups(const std::string& path, std::unordered_map<std::string, Group>& out);
    static bool saveGroups(const std::string& path, const std::unordered_map<std::string, Group>& in);

    static std::string getUserFilePath(const std::string& baseDir);
    static std::string getGroupFilePath(const std::string& baseDir);
};
