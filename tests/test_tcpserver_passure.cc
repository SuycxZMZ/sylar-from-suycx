#include "sylar/sylar.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

/**
 * @brief 自定义TcpServer类，重载handleClient方法
 */
class MyTcpServer : public sylar::TcpServer {
protected:
    virtual void handleClient(sylar::Socket::ptr client) override;
};

void MyTcpServer::handleClient(sylar::Socket::ptr client) {
    SYLAR_LOG_INFO(g_logger) << "handleClient " << *client;
    while (true) {
        char buffer[4096];
        int bytes = client->recv(buffer, sizeof(buffer) - 1);
        if (bytes <= 0) {
            // SYLAR_LOG_INFO(g_logger) << "recv failed, bytes=" << bytes;
            break;
        }
        buffer[bytes] = '\0';
        // SYLAR_LOG_INFO(g_logger) << "recv: " << buffer;

        bytes = client->send(buffer, bytes);
        if (bytes <= 0) {
            // SYLAR_LOG_INFO(g_logger) << "send failed, bytes=" << bytes;
            break;
        }
    }
    client->close();
}

void run() {
    sylar::TcpServer::ptr server(new MyTcpServer);
    auto addr = sylar::Address::LookupAny("0.0.0.0:8005");
    SYLAR_ASSERT(addr);
    std::vector<sylar::Address::ptr> addrs;
    addrs.push_back(addr);

    std::vector<sylar::Address::ptr> fails;
    while(!server->bind(addrs, fails)) {
        sleep(2);
    }
    
    SYLAR_LOG_INFO(g_logger) << "bind success, " << server->toString();
    server->start();
}

int main(int argc, char *argv[]) {
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());
    g_logger->setLevel(sylar::LogLevel::ERROR);

    sylar::IOManager iom(4);
    iom.schedule(&run);

    return 0;
}