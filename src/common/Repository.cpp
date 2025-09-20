#include "Repository.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib> // for system

bool Repository::loadUsers(const std::string& path, std::unordered_map<std::string, User>& out){
    std::ifstream fin(path);
    if(!fin) return false;
    std::string line;
    while(std::getline(fin,line)){
        if(line.empty()) continue;
        // 简单格式：id|nickname|location
        std::istringstream ss(line);
        std::string id,nick,loc;
        if(std::getline(ss,id,'|') && std::getline(ss,nick,'|') && std::getline(ss,loc,'|')){
            User u{id,nick};
            u.setLocation(loc);
            out[id]=u;
        }
    }
    return true;
}

bool Repository::saveUsers(const std::string& path, const std::unordered_map<std::string, User>& in){
    std::ofstream fout(path);
    if(!fout) return false;
    for(const auto& kv : in){
        fout << kv.first << '|' << kv.second.nickname() << '|' << kv.second.location() << "\n";
    }
    return true;
}

bool Repository::loadGroups(const std::string& path, std::unordered_map<std::string, Group>& out){
    std::ifstream fin(path);
    if(!fin) return false;
    std::string line;
    while(std::getline(fin,line)){
        if(line.empty()) continue;
        // 简单格式：groupNo|type(0=QQ,1=WX)|ownerId
        std::istringstream ss(line);
        std::string gno, t, owner;
        if(std::getline(ss,gno,'|') && std::getline(ss,t,'|') && std::getline(ss,owner,'|')){
            Group g{gno, (t=="0"?GroupType::QQ:GroupType::WeChat)};
            g.setOwner(owner);
            out[gno]=g;
        }
    }
    return true;
}

bool Repository::saveGroups(const std::string& path, const std::unordered_map<std::string, Group>& in){
    std::ofstream fout(path);
    if(!fout) return false;
    for(const auto& kv : in){
        int t = (kv.second.type()==GroupType::QQ?0:1);
        fout << kv.first << '|' << t << '|' << kv.second.owner() << "\n";
    }
    return true;
}

std::string Repository::getUserFilePath(const std::string& baseDir) {
    return baseDir + "/users.txt";
}

std::string Repository::getGroupFilePath(const std::string& baseDir) {
    return baseDir + "/groups.txt";
}
