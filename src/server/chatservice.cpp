#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <vector>
using namespace std;
using namespace muduo;

// 获取单例对象的接口函数
ChatService *ChatService::instance()
{
    static ChatService service;  // 线程安全
    return &service;
}

// 注册消息以及对应的Handler回调操作
ChatService::ChatService()
{
    // 用户基本业务管理相关事件处理回调注册  不同的业务id对应不同的回调函数
    // 将成员函数指针传递给可调用对象包装器
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

    // 群组业务管理相关事件处理回调注册
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    // 连接redis服务器
    if (_redis.connect())
    {
        // 设置上报消息的回调
        // 当有消息来到时，redis执行我们注册的回调函数handleRedisSubscribeMessage
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}

/*
*	@brief:  服务器异常，业务重置方法
*	@param:  null
*	@return: void
*/
void ChatService::reset()
{
    // 把online状态的用户，设置成offline
    _userModel.resetState();
}
 
/*
*	@brief:  获取消息对应的处理器
*	@param:  msgid：消息类型  不同消息类型对应不同的消息处理函数
*	@return: MsgHandler：msgid消息类型对应的消息处理函数
*/
MsgHandler ChatService::getHandler(int msgid)
{
    
    auto it = _msgHandlerMap.find(msgid);
    // 如果该msgid没有，则进行空操作处理并返回
    if (it == _msgHandlerMap.end())
    {
        // 返回一个默认的处理器，空操作  匿名函数 lambda表达式
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp) {
            LOG_ERROR << "msgid:" << msgid << " can not find handler!";  // 底层运算符重载实现
        };
    }
    else
    {
        return _msgHandlerMap[msgid];  // 返回该id对应的业务处理函数
    }
}

/*
*	@brief:  处理登录业务
*	@param_one:  conn：用户连接信息
*	@param_two:  js：登录对应的json格式
*	@param_three:  time：时间
*   @收到用户登录消息格式：{"msgid":XXX, "id":XXX, "password":"XXX"}
*	@return: void
*/
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int id = js["id"].get<int>();
    string pwd = js["password"];
    // 根据id值向User表中查询该id所对应的User，并返回给User
    User user = _userModel.query(id);
    if (user.getId() == id && user.getPwd() == pwd)
    {
        if (user.getState() == "online")
        {
            // 该用户已经登录，不允许重复登录
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "this account is using, input another!";
            conn->send(response.dump());
        }
        else
        {
            // 登录成功，记录用户连接信息
            {
                lock_guard<mutex> lock(_connMutex);
                // 登录成功后，记录该id用户的conn，用来后续用户之间聊天时，服务器可以把一个用户的消息转发给另一个用户
                _userConnMap.insert({id, conn});
            }

            // id用户登录成功后，向redis订阅channel(id)
            _redis.subscribe(id); 

            // 登录成功，更新用户状态信息 state offline=>online
            user.setState("online");
            _userModel.updateState(user);

            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();
            // 查询该用户是否有离线消息
            vector<string> vec = _offlineMsgModel.query(id);
            if (!vec.empty())
            {
                response["offlinemsg"] = vec;
                // 读取该用户的离线消息后，把该用户的所有离线消息删除掉
                _offlineMsgModel.remove(id);
            }

            // 查询该用户的好友信息并返回
            vector<User> userVec = _friendModel.query(id);
            if (!userVec.empty())
            {
                // vector<User>并不符合json发送格式
                vector<string> vec2;
                // 每一个User信息组成一个string
                for (User &user : userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;
            }

            // 查询用户的群组信息   Group记录了每一个群组以及对应的群成员列表
            // vector<Group>并不符合json发送格式
            vector<Group> groupuserVec = _groupModel.queryGroups(id);
            if (!groupuserVec.empty())
            {
                // group:[{groupid:[xxx, xxx, xxx, xxx]}]
                // 用vector<string>格式发送
                vector<string> groupV;   // 每一个群组信息用string表示
                for (Group &group : groupuserVec)
                {
                    json grpjson;
                    grpjson["id"] = group.getId();
                    grpjson["groupname"] = group.getName();
                    grpjson["groupdesc"] = group.getDesc();
                    vector<string> userV;  // 每一个groupuser也用string表示
                    for (GroupUser &user : group.getUsers())
                    {
                        json js;
                        js["id"] = user.getId();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        js["role"] = user.getRole();
                        userV.push_back(js.dump());
                    }
                    grpjson["users"] = userV;
                    groupV.push_back(grpjson.dump());
                }

                response["groups"] = groupV;
            }

            conn->send(response.dump());
        }
    }
    else
    {
        // 该用户不存在 / 用户存在但是密码错误，登录失败
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "id or password is invalid!";
        conn->send(response.dump());
    }
}

/*
*	@brief:  处理注册业务  用户添加注册的name和password，并由服务器返回给用户id，在线状态默认为离线
*	@param_one:  conn：用户连接信息
*	@param_two:  js：登录对应的json格式
*	@param_three:  time：时间
*   @收到用户注册消息格式：{"msgid":XXX, "name":"XXX", "password":"XXX"}
*	@return: void
*/
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    string name = js["name"];
    string pwd = js["password"];
    // 创建一个User对象
    User user;
    user.setName(name);
    user.setPwd(pwd);
    // 将该用户添加到数据库的User中
    bool state = _userModel.insert(user);
    // 插入数据库成功
    if (state)
    {
        // 注册成功
        json response;
        // 返回给用户的注册响应 用msgid字段来表示
        response["msgid"] = REG_MSG_ACK;
        // 返回给用户的errno，0表示响应成功，1表示注册失败
        response["errno"] = 0;
        response["id"] = user.getId();
        // 将注册的请求响应返回给客户端
        conn->send(response.dump());
    }
    else
    {
        // 注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
    }
}

/*
*	@brief:  处理注销业务 
*	@param_one:  conn：用户连接信息
*	@param_two:  js：登录对应的json格式
*	@param_three:  time：时间
*   @收到用户注销消息格式：{"msgid":XXX, "id":"XXX"}
*	@return: void
*/
void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it != _userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(userid); 

    // 更新用户的状态信息
    User user(userid, "", "", "offline");
    _userModel.updateState(user);
}

/*
*	@brief:  处理客户端异常退出  从map表中删除该连接信息并改变该用户的状态为offline
*	@param:  conn：用户连接信息
*	@return: void
*/
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    User user;
    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if (it->second == conn)
            {
                
                user.setId(it->first);
                // 从map表删除用户的链接信息
                _userConnMap.erase(it);
                break;
            }
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(user.getId()); 

    // 更新用户的状态信息
    if (user.getId() != -1)
    {
        user.setState("offline");
        _userModel.updateState(user);
    }
}

/*
*	@brief:  一对一聊天业务
*	@param_one:  conn：用户连接信息
*	@param_two:  js：登录对应的json格式
*	@param_three:  time：时间
*   @好友一对一聊天格式：{"msgid":XXX, "id":XXX, "name":"name", "toid":XXX, "msg":"XXX", "time": time}
*	@return: void
*/
void ChatService::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    // 接收方的id
    int toid = js["toid"].get<int>();
    // 从连接信息表中寻找该toid是否在线
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            // toid在线，转发消息   服务器主动推送消息给toid用户
            // 必须在互斥锁里面发，防止在发送之前，用户离线了。
            // 这样消息既不能成功发送给对方，也没有存到离线表里面
            it->second->send(js.dump());
            return;
        }
    }

    // 查询toid是否在线 
    User user = _userModel.query(toid);
    if (user.getState() == "online")
    {
        // 没在本服务器上，在其他服务器上，把消息发布到接收方通道上
        _redis.publish(toid, js.dump());
        return;
    }

    // toid不在线，存储离线消息
    _offlineMsgModel.insert(toid, js.dump());
}

/*
*	@brief:  添加好友业务
*	@param_one:  conn：用户连接信息
*	@param_two:  js：登录对应的json格式
*	@param_three:  time：时间
*   @添加好友格式：{"msgid": XXX, "id": XXX, "friendid": XXX}
*	@return: void
*/
void ChatService::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();

    // 存储好友信息
    _friendModel.insert(userid, friendid);
}
 
/*
*	@brief:  创建群组业务
*	@param_one:  conn：用户连接信息
*	@param_two:  js：登录对应的json格式
*	@param_three:  time：时间
*   @创建群组格式：{"msgid": XXX, "id": XXX, "groupname": XXX, "groupdesc": XXX}
*	@return: void
*/
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    // 存储新创建的群组信息
    Group group(-1, name, desc);
    if (_groupModel.createGroup(group))
    {
        // 存储群组创建人信息
        _groupModel.addGroup(userid, group.getId(), "creator");
    }
}

/*
*	@brief:  加入群组业务
*	@param_one:  conn：用户连接信息
*	@param_two:  js：登录对应的json格式
*	@param_three:  time：时间
*   @加入群组格式：{"msgid": XXX, "id": XXX, "groupid": XXX}
*	@return: void
*/
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    _groupModel.addGroup(userid, groupid, "normal");
}

/*
*	@brief:  群组聊天业务
*	@param_one:  conn：用户连接信息
*	@param_two:  js：登录对应的json格式
*	@param_three:  time：时间
*   @群组聊天格式：{"msgid": XXX, "id": XXX, "name": XXX, "groupid": XXX, "msg": XXX, "time": XXX}
*	@return: void
*/
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);
    // 根据返回的useridVec一个一个的将消息发送给群中的成员
    lock_guard<mutex> lock(_connMutex);
    for (int id : useridVec)
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end())
        {
            // 转发群消息
            it->second->send(js.dump());
        }
        else
        {
            // 查询toid是否在线 
            User user = _userModel.query(id);
            if (user.getState() == "online")
            {
                // 在其他服务器上，redis在接收方通道上发布消息
                _redis.publish(id, js.dump());
            }
            else
            {
                // 存储离线群消息
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }
}

/*
*	@brief:  从redis消息队列中获取订阅的消息 redis订阅的通道有消息来了，调用该回调函数
*	@param_one:  userid：接收方id
*	@param_two:  msg：接收方收到的消息
*	@return: void
*/
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    // 再查询一次，防止通道发布接受过程中，接收方下线
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
    {
        it->second->send(msg);
        return;
    }

    // 存储该用户的离线消息
    _offlineMsgModel.insert(userid, msg);
}