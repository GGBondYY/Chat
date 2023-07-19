#include "friendmodel.hpp"
#include "db.h"

/*
*	@brief:  添加好友
*	@param_one:  userid：用户id
*	@param_two:  friendid：想要添加的好友的id
*	@return: void
*/
void FriendModel::insert(int userid, int friendid)
{
    
    char sql[1024] = {0};
    sprintf(sql, "insert into friend values(%d, %d)", userid, friendid);

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}

/*
*	@brief:  查询用户好友列表
*	@param_one:  userid：用户id
*	@return: vector<User>：好友列表
*/
vector<User> FriendModel::query(int userid)
{
    char sql[1024] = {0};
    // user表和friend表的联合查询 返回给登录用户
    sprintf(sql, "select a.id,a.name,a.state from user a inner join friend b on b.friendid = a.id where b.userid= %d", userid);

    vector<User> vec;
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            // 把userid用户的所有离线消息放入vec中返回
            MYSQL_ROW row;
            while((row = mysql_fetch_row(res)) != nullptr)
            {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setState(row[2]);
                vec.push_back(user);
            }
            mysql_free_result(res);
            return vec;
        }
    }
    return vec;
}
