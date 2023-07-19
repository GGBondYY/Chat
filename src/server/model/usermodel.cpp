#include "usermodel.hpp"
#include "db.h"
#include <iostream>
using namespace std;

/*
*	@brief:  向User表中添加一个User
*	@param:  user：用户对象
*	@return: bool
*/
bool UserModel::insert(User &user)
{
    // 组装sql语句
    char sql[1024] = {0};
    // User的信息是在业务层得到的
    sprintf(sql, "insert into user(name, password, state) values('%s', '%s', '%s')",
            user.getName().c_str(), user.getPwd().c_str(), user.getState().c_str());

    MySQL mysql;
    if (mysql.connect())
    {
        if (mysql.update(sql))
        {
            // 获取插入成功的用户数据生成的主键id   再分配给该User，因为在业务层可能还需要使用这个User的id
            user.setId(mysql_insert_id(mysql.getConnection()));
            return true;
        }
    }

    return false;
}

/*
*	@brief:  根据用户id查询用户信息
*	@param:  id：用户id
*	@return: User：用户对象
*/
User UserModel::query(int id)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select * from user where id = %d", id);

    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            // 从表中拿到该User的一行信息
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr)
            {
                User user;
                // 下面按照数组形式拿出来的都是字符串类型
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setPwd(row[2]);
                user.setState(row[3]);
                mysql_free_result(res);
                return user;
            }
        }
    }

    return User();  // 查询失败返回空对象  默认id为-1
}

/*
*	@brief:  更新用户的状态信息
*	@param:  user：用户对象
*	@return: bool
*/
bool UserModel::updateState(User user)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "update user set state = '%s' where id = %d", user.getState().c_str(), user.getId());

    MySQL mysql;
    if (mysql.connect())
    {
        if (mysql.update(sql))
        {
            return true;
        }
    }
    return false;
}

/*
*	@brief:  重置所有在线online用户的状态信息为offline
*	@param:  null
*	@return: void
*/
void UserModel::resetState()
{
    // 组装sql语句
    char sql[1024] = "update user set state = 'offline' where state = 'online'";

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}
