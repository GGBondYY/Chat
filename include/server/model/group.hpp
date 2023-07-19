#ifndef GROUP_H
#define GROUP_H

// 用这个类表示一个Group的信息

#include "groupuser.hpp"
#include <string>
#include <vector>
using namespace std;

class Group
{
public:
    Group(int id = -1, string name = "", string desc = "")
    {
        this->id = id;
        this->name = name;
        this->desc = desc;
    }

    void setId(int id) { this->id = id; }
    void setName(string name) { this->name = name; }
    void setDesc(string desc) { this->desc = desc; }

    int getId() { return this->id; }
    string getName() { return this->name; }
    string getDesc() { return this->desc; }
    vector<GroupUser> &getUsers() { return this->users; }

private:
    int id;                     // 组id
    string name;                // 组名称
    string desc;                // 组功能描述
    vector<GroupUser> users;    // 群组的成员  GroupUser的设计是因为成员有了"角色"这个描述

};

#endif