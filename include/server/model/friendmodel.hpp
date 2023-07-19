#ifndef FRIENDMODEL_H
#define FRIENDMODEL_H

#include "user.hpp"
#include <vector>
using namespace std;
// Friend表的数据操作类
// 在业务层直接使用这个类对Friend表进行操作

class FriendModel
{
public:
    // 添加好友
    void insert(int userid, int friendid);

    // 返回用户好友列表  
    vector<User> query(int userid);
};

#endif