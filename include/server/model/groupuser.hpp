#ifndef GROUPUSER_H
#define GROUPUSER_H

#include "user.hpp"
// 描述群组中的一个用户基本信息，与User类不同的是，它多了一个User在群组中的role信息
// 从User类直接继承，复用User的其它信息
class GroupUser : public User
{
public:
    void setRole(string role) { this->role = role; }
    string getRole() { return this->role; }

private:
    string role;
};

#endif