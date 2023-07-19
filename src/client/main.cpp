#include "json.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <unordered_map>
#include <functional>
using namespace std;
using json = nlohmann::json;

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <atomic>

#include "group.hpp"
#include "user.hpp"
#include "public.hpp"

// 记录当前系统登录的用户基本信息
User g_currentUser;
// 记录当前登录用户的好友列表信息
vector<User> g_currentUserFriendList;
// 记录当前登录用户的群组列表信息  
vector<Group> g_currentUserGroupList;

// 控制主菜单页面程序  用于当用户注销时，返回主界面
bool isMainMenuRunning = false;

// 用于读写线程之间的通信 client客户端需要两个线程，一个读线程接收消息，一个写线程发送消息
sem_t rwsem;
// 记录登录状态
atomic_bool g_isLoginSuccess{false};

// 接收线程  接收服务器的消息
void readTaskHandler(int clientfd);

// 获取系统时间（聊天信息需要添加时间信息）
string getCurrentTime();

// 主聊天页面程序
void mainMenu(int);

// 显示当前登录成功用户的基本信息  在主界面显示
void showCurrentUserData();

// 聊天客户端程序实现，main线程用作发送线程，子线程用作接收线程
int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }

    // 解析通过命令行参数传递的ip和port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 创建client端的socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd)
    {
        cerr << "socket create error" << endl;
        exit(-1);
    }

    // 填写client需要连接的server信息ip+port
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    // client和server进行连接
    if (-1 == connect(clientfd, (sockaddr *)&server, sizeof(sockaddr_in)))
    {
        cerr << "connect server error" << endl;
        close(clientfd);
        exit(-1);
    }

    // 初始化读写线程通信用的信号量
    sem_init(&rwsem, 0, 0);

    // 连接服务器成功，启动接收子线程
    std::thread readTask(readTaskHandler, clientfd); // pthread_create
    readTask.detach();                               // pthread_detach

    // main线程用于接收用户输入，负责发送数据
    for (;;)
    {
        // 显示首页面菜单 登录、注册、退出
        cout << "========================" << endl;
        cout << "1. login" << endl;         // 登录
        cout << "2. register" << endl;      // 注册
        cout << "3. quit" << endl;          // 退出
        cout << "========================" << endl;
        cout << "choice:";
        int choice = 0;
        cin >> choice;  // cin不读回车
        cin.get(); // 读掉缓冲区残留的回车

        switch (choice)
        {
        case 1: // login业务   
        {
            int id = 0;             
            char pwd[50] = {0};
            cout << "userid:";
            cin >> id;                  // 用户输入id
            cin.get(); // 读掉缓冲区残留的回车
            cout << "userpassword:";    // 用户输入密码
            cin.getline(pwd, 50);

            json js;
            js["msgid"] = LOGIN_MSG;
            js["id"] = id;
            js["password"] = pwd;
            string request = js.dump();
            // 还没有登陆成功  登陆成功之后进入主界面
            g_isLoginSuccess = false;
            // 发送给服务器
            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1)
            {
                cerr << "send login msg error:" << request << endl;
            }
            // 等待子线程读线程处理完接收到登录响应之后解除阻塞
            sem_wait(&rwsem); // 等待信号量，由子线程处理完登录的响应消息后，通知这里
            // 在子线程处理登录响应中将其设置为true，进入聊天主界面    
            if (g_isLoginSuccess) 
            {
                // 进入聊天主菜单页面
                isMainMenuRunning = true;       // 主界面在运行
                mainMenu(clientfd);             // 传入对话用的fd，文件描述符
            }
            else{
                cout << "login defeat!" << endl;
            }
        }
        break;
        case 2: // register业务
        {
            char name[50] = {0};
            char pwd[50] = {0};
            cout << "username:";
            cin.getline(name, 50);    // 输入注册的昵称  cin遇见空格就结束了，name中可能会有空格 默认遇见回车结束
            cout << "userpassword:";
            cin.getline(pwd, 50);     // 输入注册的密码

            json js;
            js["msgid"] = REG_MSG;
            js["name"] = name;
            js["password"] = pwd;
            string request = js.dump();
            // 发送给服务器
            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1)
            {
                cerr << "send reg msg error:" << request << endl;
            }
            
            sem_wait(&rwsem); // 等待信号量，子线程处理完注册响应消息会通知
        }
        break;
        case 3: // quit业务
            // 关闭文件描述符  服务器会收到断开连接消息，将用户的状态设置为offline
            close(clientfd);
            // 销毁信号量
            sem_destroy(&rwsem);
            // 退出
            exit(0);
        default:
            // 输入命令错误
            cerr << "invalid input!" << endl;
            break;
        }
    }

    return 0;
}

/*
*	@brief:  处理注册的响应逻辑
*	@param:  responsejs：注册响应消息
*	@return: void
*/
void doRegResponse(json &responsejs)
{
    if (0 != responsejs["errno"].get<int>()) // 注册失败
    {
        cerr << "name is already exist, register error!" << endl;
    }
    else // 注册成功
    {
        cout << "name register success, userid is " << responsejs["id"]
                << ", do not forget it!" << endl;
    }
}

/*
*	@brief:  处理登录的响应逻辑
*	@param:  responsejs：登录响应消息
*	@return: void
*/
void doLoginResponse(json &responsejs)
{
    // 如果json的errno字段不是0，说明登陆失败了
    if (0 != responsejs["errno"].get<int>()) 
    {
        cerr << responsejs["errmsg"] << endl;
        // 不能进入到主界面
        g_isLoginSuccess = false;
    }
    // 登录成功
    else 
    {
        // 记录当前用户的id和name
        g_currentUser.setId(responsejs["id"].get<int>());
        g_currentUser.setName(responsejs["name"]);

        // 记录当前用户的好友列表信息
        if (responsejs.contains("friends"))
        {
            
            g_currentUserFriendList.clear();

            vector<string> vec = responsejs["friends"];
            for (string &str : vec)
            {
                json js = json::parse(str);
                User user;
                user.setId(js["id"].get<int>());
                user.setName(js["name"]);
                user.setState(js["state"]);
                g_currentUserFriendList.push_back(user);
            }
        }

        // 记录当前用户的群组列表信息  包括：群组id，群组名，群组描述，群成员列表
        if (responsejs.contains("groups"))
        {
            
            g_currentUserGroupList.clear();

            vector<string> vec1 = responsejs["groups"];
            for (string &groupstr : vec1)
            {
                json grpjs = json::parse(groupstr);
                Group group;
                group.setId(grpjs["id"].get<int>());
                group.setName(grpjs["groupname"]);
                group.setDesc(grpjs["groupdesc"]);

                vector<string> vec2 = grpjs["users"];
                for (string &userstr : vec2)
                {
                    GroupUser user;
                    json js = json::parse(userstr);
                    user.setId(js["id"].get<int>());
                    user.setName(js["name"]);
                    user.setState(js["state"]);
                    user.setRole(js["role"]);
                    group.getUsers().push_back(user);
                }

                g_currentUserGroupList.push_back(group);
            }
        }

        // 显示登录用户的基本信息
        showCurrentUserData();

        // 显示当前用户的离线消息  个人聊天信息或者群组消息
        if (responsejs.contains("offlinemsg"))
        {
            vector<string> vec = responsejs["offlinemsg"];
            for (string &str : vec)
            {
                json js = json::parse(str);
                // time + [id] + name + " said: " + xxx
                if (ONE_CHAT_MSG == js["msgid"].get<int>())
                {
                    cout << "个人消息: " <<js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                            << " said: " << js["msg"].get<string>() << endl;
                }
                else
                {
                    cout << "群消息[" << js["groupid"] << "]:" << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                            << " said: " << js["msg"].get<string>() << endl;
                }
            }
        }
        // 将登陆成功设置为true
        g_isLoginSuccess = true;
    }
}

/*
*	@brief:  子线程 - 接收线程
*	@param:  clientfd：客户端的文件描述符
*	@return: void
*/
void readTaskHandler(int clientfd)
{
    for (;;)
    {
        char buffer[1024] = {0};
        int len = recv(clientfd, buffer, 1024, 0);
        if (-1 == len || 0 == len)
        {
            close(clientfd);
            exit(-1);
        }

        // 接收ChatServer转发的数据，反序列化生成json数据对象
        json js = json::parse(buffer);
        
        int msgtype = js["msgid"].get<int>();       // 数据类型字段 判断接收到的数据类型
        // 单独聊天消息
        if (ONE_CHAT_MSG == msgtype)
        {
            cout << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                 << " said: " << js["msg"].get<string>() << endl;
            continue;
        }
        // 群聊消息
        if (GROUP_CHAT_MSG == msgtype)
        {
            cout << "群消息[" << js["groupid"] << "]:" << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                 << " said: " << js["msg"].get<string>() << endl;
            continue;
        }
        // 登录响应消息   
        if (LOGIN_MSG_ACK == msgtype)
        {
            doLoginResponse(js);    // 处理登录响应的业务逻辑
            sem_post(&rwsem);       // 通知主线程，登录结果处理完成
            continue;
        }
        // 注册响应消息
        if (REG_MSG_ACK == msgtype)
        {
            doRegResponse(js);
            sem_post(&rwsem);    // 通知主线程，注册结果处理完成
            continue;
        }
    }
}
 
/*
*	@brief:  显示当前登录成功用户的基本信息
*	@param:  null
*	@return: void
*/
void showCurrentUserData()
{
    cout << "======================login user======================" << endl;
    cout << "current login user => id:" << g_currentUser.getId() << " name:" << g_currentUser.getName() << endl;
    cout << "----------------------friend list---------------------" << endl;
    if (!g_currentUserFriendList.empty())
    {
        for (User &user : g_currentUserFriendList)
        {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }
    cout << "----------------------group list----------------------" << endl;
    if (!g_currentUserGroupList.empty())
    {
        for (Group &group : g_currentUserGroupList)
        {
            cout << group.getId() << " " << group.getName() << " " << group.getDesc() << endl;
            for (GroupUser &user : group.getUsers())
            {
                cout << user.getId() << " " << user.getName() << " " << user.getState()
                     << " " << user.getRole() << endl;
            }
        }
    }
    cout << "======================================================" << endl;
}

/*============用户指令对应函数============*/
// "help" command handler
void help(int fd = 0, string str = "");
// "chat" command handler
void chat(int, string);
// "addfriend" command handler
void addfriend(int, string);
// "creategroup" command handler
void creategroup(int, string);
// "addgroup" command handler
void addgroup(int, string);
// "groupchat" command handler
void groupchat(int, string);
// "loginout" command handler
void loginout(int, string);

// 系统支持的客户端命令列表   客户端登陆上之后给客户端显示一个help命令列表，让客户端根据命令列表的格式去进行聊天等操作
unordered_map<string, string> commandMap = {
    {"help", "显示所有支持的命令，格式 -> help"},
    {"chat", "一对一聊天，格式 -> chat:friendid:message"},
    {"addfriend", "添加好友，格式 -> addfriend:friendid"},
    {"creategroup", "创建群组，格式 -> creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组，格式 -> addgroup:groupid"},
    {"groupchat", "群聊，格式 -> groupchat:groupid:message"},
    {"loginout", "注销，格式 -> loginout"}};

// 注册系统支持的客户端命令处理
unordered_map<string, function<void(int, string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"loginout", loginout}};
 
/*
*	@brief:  主聊天页面程序  主线程
*	@param:  clientfd：客户端文件描述符
*	@return: void
*/
void mainMenu(int clientfd)
{
    // 首先给客户端显示所需执行各种操作的基本命令列表
    help();

    char buffer[1024] = {0};
    // 用户注销之后，应回到主界面  isMainMenuRunning为false
    while (isMainMenuRunning)
    {
        // 接收客户端执行的命令  此时客户端应按照help给出的指令进行接下来的操作
        cin.getline(buffer, 1024);
        string commandbuf(buffer);
        string command; // 存储命令
        int idx = commandbuf.find(":");     // 查询客户端输入命令冒号前的指示（help和loginout除外）
        if (-1 == idx)
        {
            command = commandbuf;   // 客户端输入的命令可能是help或者loginout
        }
        else
        {
            command = commandbuf.substr(0, idx);  // 获取命令的具体信息
        }
        auto it = commandHandlerMap.find(command);      // 从命令函数映射表中寻找该命令对应的执行函数
        if (it == commandHandlerMap.end())        // 没有找到，输入的为无效的命令
        {
            cerr << "invalid input command!" << endl;
            continue;
        }

        // 调用相应命令的事件处理回调
        it->second(clientfd, commandbuf.substr(idx + 1, commandbuf.size() - idx)); // 调用命令处理方法
    }
}

/*
*	@brief:  "help" command handler
*	@param_one:  int：客户端文件描述符
*	@param_two:  string：客户端具体指令
*	@return: void
*/
void help(int, string)
{
    cout << "show command list >>> " << endl;
    for (auto &p : commandMap)
    {
        cout << p.first << " : " << p.second << endl;
    }
    cout << endl;
}
 
/*
*	@brief:  "addfriend" command handler 添加好友
*	@param_one:  clientfd：客户端文件描述符
*	@param_two:  str：客户端具体指令
*	@return: void
*/
void addfriend(int clientfd, string str)
{
    // friendid
    int friendid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["friendid"] = friendid;
    string buffer = js.dump();
    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send addfriend msg error -> " << buffer << endl;
    }
}
 
/*
*	@brief:  "chat" command handler 好友聊天
*	@param_one:  clientfd：客户端文件描述符
*	@param_two:  str：客户端具体指令
*	@return: void
*/
void chat(int clientfd, string str)
{
    // friendid:message
    int idx = str.find(":"); 
    if (-1 == idx)
    {
        cerr << "chat command invalid!" << endl;
        return;
    }
    // friendid
    int friendid = atoi(str.substr(0, idx).c_str());
    // message
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["toid"] = friendid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send chat msg error -> " << buffer << endl;
    }
}

/*
*	@brief:  "creategroup" command handler 创建群组
*	@param_one:  clientfd：客户端文件描述符
*	@param_two:  str：客户端具体指令
*	@return: void
*/
void creategroup(int clientfd, string str) 
{
    // groupname:groupdesc
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "creategroup command invalid!" << endl;
        return;
    }
    // 群组名
    string groupname = str.substr(0, idx);
    // 群组描述
    string groupdesc = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;
    string buffer = js.dump();
    
    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send creategroup msg error -> " << buffer << endl;
    }
}

/*
*	@brief:  "addgroup" command handler 添加群组
*	@param_one:  clientfd：客户端文件描述符
*	@param_two:  str：客户端具体指令
*	@return: void
*/
void addgroup(int clientfd, string str)
{
    // groupid
    int groupid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send addgroup msg error -> " << buffer << endl;
    }
}
  
/*
*	@brief:  "groupchat" command handler 群组聊天
*	@param_one:  clientfd：客户端文件描述符
*	@param_two:  str：客户端具体指令
*	@return: void
*/ 
void groupchat(int clientfd, string str)
{
    // groupid:message 
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "groupchat command invalid!" << endl;
        return;
    }
    // groupid
    int groupid = atoi(str.substr(0, idx).c_str());
    // message
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["groupid"] = groupid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send groupchat msg error -> " << buffer << endl;
    }
}

/*
*	@brief:  "loginout" command handler 注销
*	@param_one:  clientfd：客户端文件描述符
*	@param_two:  str：客户端具体指令
*	@return: void
*/ 
void loginout(int clientfd, string)
{
    json js;
    js["msgid"] = LOGINOUT_MSG;
    js["id"] = g_currentUser.getId();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send loginout msg error -> " << buffer << endl;
    }
    else
    {
        // 用户注销之后，应回到主界面
        isMainMenuRunning = false;
    }   
}

/*
*	@brief:  获取系统时间（聊天信息需要添加时间信息）
*	@param:  null
*	@return: void
*/ 
string getCurrentTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return std::string(date);
}
