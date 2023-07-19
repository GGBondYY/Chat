#include "groupmodel.hpp"
#include "db.h"
  
/*
*	@brief:  创建群组
*	@param:  group：群组的基本信息  此时并没有群成员
*	@return: bool
*/
bool GroupModel::createGroup(Group &group)
{
    
    char sql[1024] = {0};
    sprintf(sql, "insert into allgroup(groupname, groupdesc) values('%s', '%s')",
            group.getName().c_str(), group.getDesc().c_str());

    MySQL mysql;
    if (mysql.connect())
    {
        if (mysql.update(sql))
        {
            group.setId(mysql_insert_id(mysql.getConnection()));
            return true;
        }
    }

    return false;
}

/*
*	@brief:  加入群组
*	@param_one:  userid：要加入群组的用户id
*	@param_two:  groupid：要加入的群组id
*	@param_three:  role：该用户在该群组中的角色
*	@return: void
*/
void GroupModel::addGroup(int userid, int groupid, string role)
{
    char sql[1024] = {0};
    sprintf(sql, "insert into groupuser values(%d, %d, '%s')",
            groupid, userid, role.c_str());

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}

/*
*	@brief:  查询用户所在群组信息
*	@param_one:  userid：要查询的用户
*	@return: vector<Group>：该用户所在的所有群组的信息
*/  
vector<Group> GroupModel::queryGroups(int userid)
{
    // 联合查询AllGroup和GroupUser
    char sql[1024] = {0};
    sprintf(sql, "select a.id,a.groupname,a.groupdesc from allgroup a inner join \
         groupuser b on a.id = b.groupid where b.userid=%d",
            userid);
    // 得到该id所在的所有组，该组包含了组id、组名、组功能描述以及组的成员
    vector<Group> groupVec;

    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            // 查出userid所有的群组信息
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                Group group;
                group.setId(atoi(row[0]));
                group.setName(row[1]);
                group.setDesc(row[2]);
                groupVec.push_back(group);
            }
            mysql_free_result(res);
        }
    }

    // 联合查询GroupUser和User
    for (Group &group : groupVec)
    {
        sprintf(sql, "select a.id,a.name,a.state,b.grouprole from user a \
            inner join groupuser b on b.userid = a.id where b.groupid=%d",
                group.getId());

        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                GroupUser user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setState(row[2]);
                user.setRole(row[3]);
                group.getUsers().push_back(user);
            }
            mysql_free_result(res);
        }
    }
    return groupVec;
}

/*
*	@brief:  根据指定的groupid查询群组用户id列表，除userid自己，主要用于群聊业务给群组其它成员群发消息  返回该群组除自己之外的所有用户id
*	@param_one:  userid：要查询的用户
*	@param_two:  groupid：该用户想要查询的组
*	@return: vector<int>：用户查询组除用户本身的所有人的id列表
*/ 
vector<int> GroupModel::queryGroupUsers(int userid, int groupid)
{
    char sql[1024] = {0};
    sprintf(sql, "select userid from groupuser where groupid = %d and userid != %d", groupid, userid);

    vector<int> idVec;
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                idVec.push_back(atoi(row[0]));
            }
            mysql_free_result(res);
        }
    }
    return idVec;
}
