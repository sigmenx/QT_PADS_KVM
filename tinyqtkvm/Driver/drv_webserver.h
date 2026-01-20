#ifndef DRV_WEBSERVER_H
#define DRV_WEBSERVER_H

#include <vector>
#include <string>
#include <cstdint> // for uint8_t, uint64_t

class WebServer {
public:
    // 构造函数：初始化 Socket 并绑定端口，设置非阻塞模式
    WebServer(int port);
    
    // 析构函数：关闭所有连接
    ~WebServer();

    // 核心轮询函数：
    // 1. 尝试 accept 新连接
    // 2. 如果是 HTTP GET，发送 index.html
    // 3. 如果是 WebSocket Upgrade，完成握手并加入 clients_ 列表
    void handle_new_connections();

    // 广播二进制数据给所有已连接的 WebSocket 客户端
    void broadcast(uint8_t* data, int len);

    std::vector<std::vector<uint8_t>> process_client_messages();

    // 获取当前连接的客户端数量
    int GetClientNumber();

private:
    int server_fd_;
    std::vector<int> clients_; // 存储所有 WebSocket 客户端的 socket fd

    // 辅助函数：WebSocket 握手逻辑
    bool do_handshake(int client_fd, char* request_buffer);
    
    // 辅助函数：Base64 编码 (用于握手验证)
    char* base64_encode(const unsigned char* input, int length);
    
    // 辅助函数：网络字节序转换 (64位)
    uint64_t htonll(uint64_t val);
};

#endif // WEBSERVER_H
