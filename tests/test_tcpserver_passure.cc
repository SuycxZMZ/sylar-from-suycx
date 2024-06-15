/**
 * @file test_tcp_server.cc
 * @brief TcpServer类测试
 * @version 0.1
 * @date 2021-09-18
 */
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
    char recv_buf[4096] = {0};
    int len = client->recv(recv_buf, sizeof(recv_buf)); // 这里有读超时，由tcp_server.read_timeout配置项进行配置，默认120秒
    client->send(recv_buf, len);
    // client->close();
}

void run() {
    sylar::TcpServer::ptr server(new MyTcpServer);
    auto addr = sylar::Address::LookupAny("0.0.0.0:8000");
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