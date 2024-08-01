#include <iostream>
#include <vector>
#include <thread>
#include <random>
#include <chrono>
#include <cstring>
#include <cassert>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

// 参数配置
const int NUM_CONNECTIONS = 1000;
const int NUM_REQUESTS = 1000;
const int MIN_MESSAGE_SIZE = 200;
const int MAX_MESSAGE_SIZE = 400;
const char* SERVER_IP = "127.0.0.1";
const int SERVER_PORT = 8005;

// 生成随机字符串
std::string generate_random_string(size_t length) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(alphanum) - 2);

    std::string s;
    s.resize(length);
    for (size_t i = 0; i < length; ++i)
        s[i] = alphanum[dis(gen)];

    return s;
}

// 处理请求
void handle_connection(int connection_id) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection failed" << std::endl;
        close(sock);
        return;
    }

    char recv_buffer[MAX_MESSAGE_SIZE];
    for (int i = 0; i < NUM_REQUESTS; ++i) {
        // 生成随机长度字符串
        int length = MIN_MESSAGE_SIZE + rand() % (MAX_MESSAGE_SIZE - MIN_MESSAGE_SIZE + 1);
        std::string message = generate_random_string(length);

        // 发送请求
        auto start_time = std::chrono::high_resolution_clock::now();
        send(sock, message.c_str(), length, 0);

        // 接收回发数据
        int bytes_received = recv(sock, recv_buffer, length, 0);
        auto end_time = std::chrono::high_resolution_clock::now();

        if (bytes_received != length || std::memcmp(message.c_str(), recv_buffer, length) != 0) {
            std::cerr << "Data mismatch or incomplete" << std::endl;
        }

        // 计算延迟
        std::chrono::duration<double> request_time = end_time - start_time;
        // std::cout << "Connection " << connection_id << ", Request " << i 
        //           << ", Time: " << request_time.count() << "s" << std::endl;
    }

    close(sock);
}

int main() {
    std::vector<std::thread> threads;
    auto start_time = std::chrono::high_resolution_clock::now();

    // 创建连接
    for (int i = 0; i < NUM_CONNECTIONS; ++i) {
        threads.emplace_back(handle_connection, i);
    }

    // 等待所有线程完成
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> total_time = end_time - start_time;

    std::cout << "Total time: " << total_time.count() << "s" << std::endl;
    std::cout << "Total throughput: " << (NUM_CONNECTIONS * NUM_REQUESTS) / total_time.count() << " requests/s" << std::endl;

    return 0;
}
