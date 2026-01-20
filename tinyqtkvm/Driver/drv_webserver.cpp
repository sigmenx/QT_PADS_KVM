#include "drv_webserver.h"

// --- 引入 Qt 头文件 ---
#include <QFile>
#include <QByteArray>
#include <QDebug>

#include <QDir>

// --- 保留原有的 C/C++ 头文件 ---
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <iostream>
#include <algorithm> // 只需要 algorithm，不需要 fstream 和 sstream 了

// --- 辅助函数：使用 QFile 读取资源文件 ---
std::string load_file_content(const std::string& filename) {
    // 将 std::string 转为 QString 以适配 QFile
    QString qFilename = QString::fromStdString(filename);
    QFile file(qFilename);

// === [调试代码] 打印所有资源文件路径 ===
qDebug() << "=== Dumping Qt Resources ===";
QDir dir(":/"); // 从根目录开始遍历
for (QString file : dir.entryList()) {
    qDebug() << "Found:" << file;
}
// 如果你在 qrc 里加了前缀，比如 /web，那就遍历 :/web
// ======================================

    // 以只读和文本模式打开
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "[WebServer] Error: Cannot open Qt Resource" << qFilename;
        return "";
    }


    // 读取所有内容
    QByteArray data = file.readAll();
    file.close();
    // 转回 std::string 返回
    return data.toStdString();
}

WebServer::WebServer(int port) {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int flags = fcntl(server_fd_, F_GETFL, 0);
    fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(1);
    }

    if (listen(server_fd_, 5) < 0) {
        perror("Listen failed");
        exit(1);
    }

    printf("[WebServer] Running at http://0.0.0.0:%d\n", port);
}

WebServer::~WebServer() {
    for (int fd : clients_) {
        close(fd);
    }
    close(server_fd_);
}

void WebServer::handle_new_connections() {
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    
    int client_fd = accept(server_fd_, (struct sockaddr *)&client_addr, &addrlen);
    if (client_fd < 0) return; 

    // 读取请求头
    char buffer[2048] = {0};
    int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }

    // --- 路由逻辑 ---

    // 1. WebSocket 升级请求
    if (strstr(buffer, "Upgrade: websocket")) {
        if (do_handshake(client_fd, buffer)) {
            int flags = fcntl(client_fd, F_GETFL, 0);
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
            clients_.push_back(client_fd);
            printf("[WebServer] New Client: %s\n", inet_ntoa(client_addr.sin_addr));
        } else {
            close(client_fd);
        }
    }
    // 2. 请求 /jmuxer.min.js
    else if (strstr(buffer, "GET /jmuxer.min.js HTTP")) {
        std::string content = load_file_content(":/jmuxer.min.js");
        if (content.empty()) {
            const char* not_found = "HTTP/1.1 404 Not Found\r\n\r\n";
            send(client_fd, not_found, strlen(not_found), 0);
        } else {
            std::string header = "HTTP/1.1 200 OK\r\nContent-Type: application/javascript\r\nContent-Length: " 
                               + std::to_string(content.size()) + "\r\n\r\n";
            send(client_fd, header.c_str(), header.size(), 0);
            send(client_fd, content.c_str(), content.size(), 0);
        }
        close(client_fd); // 发送完文件即关闭 HTTP 短连接
    }
    // 3. 请求 / 或 /index.html
    else if (strstr(buffer, "GET / HTTP") || strstr(buffer, "GET /index.html HTTP")) {
        std::string content = load_file_content(":/index.html");
        if (content.empty()) {
            const char* not_found = "HTTP/1.1 404 Not Found\r\n\r\nFile index.html not found on server.";
            send(client_fd, not_found, strlen(not_found), 0);
        } else {
            std::string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " 
                               + std::to_string(content.size()) + "\r\n\r\n";
            send(client_fd, header.c_str(), header.size(), 0);
            send(client_fd, content.c_str(), content.size(), 0);
        }
        close(client_fd);
    }
    // 其他请求忽略
    else {
        close(client_fd);
    }
}

void WebServer::broadcast(uint8_t* data, int len) {
    if (clients_.empty()) return;

    uint8_t frame_header[14];
    int header_len = 0;
    frame_header[0] = 0x82;

    if (len <= 125) {
        frame_header[1] = len;
        header_len = 2;
    } else if (len <= 65535) {
        frame_header[1] = 126;
        *(uint16_t*)&frame_header[2] = htons(len);
        header_len = 4;
    } else {
        frame_header[1] = 127;
        *(uint64_t*)&frame_header[2] = htonll(len);
        header_len = 10;
    }

    auto it = clients_.begin();
    while (it != clients_.end()) {
        int fd = *it;
        bool success = true;
        if (send(fd, frame_header, header_len, MSG_NOSIGNAL) < 0) success = false;
        if (success && send(fd, data, len, MSG_NOSIGNAL) < 0) success = false;

        if (!success) {
            close(fd);
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
}

uint64_t WebServer::htonll(uint64_t val) {
    return ((uint64_t)htonl(val & 0xFFFFFFFF) << 32) | htonl(val >> 32);
}

char* WebServer::base64_encode(const unsigned char* input, int length) {
    BIO *bmem, *b64;
    BUF_MEM *bptr;
    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, input, length);
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);
    char *buff = (char *)malloc(bptr->length + 1);
    memcpy(buff, bptr->data, bptr->length);
    buff[bptr->length] = 0;
    BIO_free_all(b64);
    return buff;
}

bool WebServer::do_handshake(int client_fd, char* buf) {
    char *key_start = strstr(buf, "Sec-WebSocket-Key");
    if (!key_start) return false;
    key_start = strchr(key_start, ':');
    if (!key_start) return false;
    key_start++; 
    while (*key_start == ' ') key_start++;
    char *key_end = strstr(key_start, "\r\n");
    if (!key_end) return false;

    char client_key[128] = {0};
    int key_len = key_end - key_start;
    if (key_len > 127) key_len = 127;
    memcpy(client_key, key_start, key_len);

    const char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char combined[256];
    sprintf(combined, "%s%s", client_key, guid);

    unsigned char sha1_hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char *)combined, strlen(combined), sha1_hash);
    char *accept_key = base64_encode(sha1_hash, SHA_DIGEST_LENGTH);

    char response[512];
    sprintf(response, 
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n", 
        accept_key);
    
    send(client_fd, response, strlen(response), 0);
    free(accept_key);
    return true;
}
std::vector<std::vector<uint8_t>> WebServer::process_client_messages() {
    std::vector<std::vector<uint8_t>> messages;
    uint8_t buf[2048]; 

    auto it = clients_.begin();
    while (it != clients_.end()) {
        int fd = *it;
        int n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        
        if (n > 0) {
            uint8_t opcode = buf[0] & 0x0F;
            uint8_t payload_len = buf[1] & 0x7F;

            if (opcode == 0x8) { // Close
                close(fd);
                it = clients_.erase(it);
                continue;
            }

            // Opcode 0x1 (Text) 或 0x2 (Binary) 均处理
            if ((opcode == 0x1 || opcode == 0x2) && payload_len > 0) {
                // 简单处理短包
                if (payload_len < 126) {
                    uint32_t masking_key;
                    memcpy(&masking_key, &buf[2], 4);
                    
                    std::vector<uint8_t> decoded(payload_len);
                    uint8_t* mask = (uint8_t*)&masking_key;
                    
                    for (int i = 0; i < payload_len; i++) {
                        decoded[i] = buf[6 + i] ^ mask[i % 4];
                    }
                    messages.push_back(decoded);
                }
            }
        } else if (n == 0) {
            close(fd);
            it = clients_.erase(it);
            continue;
        }
        ++it;
    }
    return messages;
}

// 获取客户端数量
int WebServer::GetClientNumber() {
    return clients_.size();
}

