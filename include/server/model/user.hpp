#ifndef USER_H
#define USER_H
// User表的ORM类
// 该User类对应数据库的User表，用这个类表示一个USER的信息

#include <string>
using namespace std;

class User
{
public:
    User(int id = -1, string name = "", string pwd = "", string state = "offline")
    {
        this->id = id;
        this->name = name;
        this->password = pwd;
        this->state = state;        // 默认为离线状态
    }

    void setId(int id) { this->id = id; }                   // 设置id
    void setName(string name) { this->name = name; }        // 设置昵称
    void setPwd(string pwd) { this->password = pwd; }       // 设置密码
    void setState(string state) { this->state = state; }    // 设置在线状态

    int getId() { return this->id; }                        // 获取id
    string getName() { return this->name; }                 // 获取昵称
    string getPwd() { return this->password; }              // 获取密码
    string getState() { return this->state; }               // 获取在线状态

protected:
    //  表中字段
    int id;             // 用户id
    string name;        // 用户昵称
    string password;    // 用户密码
    string state;       // 在线状态
};

#endif