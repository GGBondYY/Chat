# chat
基于muduo网络库的可以工作在nginx tcp负载均衡环境中的集群聊天服务器和客户端  

编译：
cd /build
rm -rf *
cmake ..
make

负载均衡配置：
nginx tcp loadbalance config
stream {
    upstream MyServer{
        server 127.0.0.1:6000 weight=1 max_fails=3 fail_timeout=30s;
        server 127.0.0.1:6000 weight=1 max_fails=3 fail_timeout=30s;
    }   
    server {
        proxy_connect_timeout ls;  
        listen 8000;    # nginx监听的端口号
        proxy_pass MyServer;  # 标记
        tcp_nodelay on;
    }   
}

