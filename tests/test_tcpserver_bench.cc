#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <mutex>

std::mutex mtx;  // Mutex for protecting shared resources

// Function to generate a random string of specified length
std::string generateRandomString(size_t length) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string result;
    result.resize(length);

    std::default_random_engine rng(std::random_device{}());
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);

    for (size_t i = 0; i < length; ++i) {
        result[i] = charset[dist(rng)];
    }

    return result;
}

// Function to handle a single connection
void handleConnection(const std::string& server_ip, int server_port, int num_requests, std::vector<double>& latencies) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[2048];

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Socket creation error\n";
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported\n";
        return;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection failed\n";
        return;
    }

    for (int i = 0; i < num_requests; ++i) {
        std::string message = generateRandomString(200);
        auto start_time = std::chrono::high_resolution_clock::now();

        send(sock, message.c_str(), message.size(), 0);
        ssize_t valread = read(sock, buffer, message.size());

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;

        if ((std::size_t)valread == message.size()) {
            std::lock_guard<std::mutex> lock(mtx);
            latencies.push_back(elapsed.count());
        } else {
            std::cerr << "Request " << i + 1 << " failed. Expected " << message.size() << " bytes, got " << valread << " bytes\n";
        }
    }

    close(sock);
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> <server_port> <num_connections> <num_requests_per_connection>\n";
        return 1;
    }

    std::string server_ip = argv[1];
    int server_port = std::stoi(argv[2]);
    int num_connections = std::stoi(argv[3]);
    int num_requests_per_connection = std::stoi(argv[4]);

    std::vector<std::thread> threads;
    std::vector<double> latencies;

    auto total_start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_connections; ++i) {
        threads.emplace_back(handleConnection, server_ip, server_port, num_requests_per_connection, std::ref(latencies));
    }

    for (auto& t : threads) {
        t.join();
    }

    auto total_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> total_elapsed = total_end_time - total_start_time;

    double total_time = total_elapsed.count();
    double average_latency = 0.0;
    for (const auto& latency : latencies) {
        average_latency += latency;
    }
    average_latency /= latencies.size();

    double throughput = latencies.size() / total_time;

    std::cout << "Average Latency: " << average_latency << " seconds\n";
    std::cout << "Throughput: " << throughput << " requests per second\n";

    return 0;
}
