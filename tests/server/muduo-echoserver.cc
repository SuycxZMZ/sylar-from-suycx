#include <tinymuduo/tcpserver.h>

#include <functional>
#include <string>

class EchoServer
{
public:
    EchoServer(EventLoop *loop,
               const InetAddress &listenAddr,
               const std::string &name) : 
               m_loop(loop),
               m_server(m_loop, listenAddr, name)
    {
        LOG_DEBUG("EchoServer::EchoServer()");
        // 注册回调
        m_server.setConnCallBack(std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        m_server.setMsgCallBack(std::bind(&EchoServer::onMessage, this,
                                          std::placeholders::_1,
                                          std::placeholders::_2,
                                          std::placeholders::_3));
        // 设置线程数
        m_server.setThreadNum(3);
    }
    void start()
    {
        m_server.start();
    }

private:
    void onConnection(const TcpConnectionPtr &conn)
    {
        if (conn->connected())
        {
            LOG_INFO("client:%s connected", conn->peerAddr().toIpPort().c_str());
        }
        else
        {
            LOG_INFO("client:%s disconnected", conn->peerAddr().toIpPort().c_str());
        }
    }

    void onMessage(const TcpConnectionPtr &conn,
                   Buffer *buf,
                   Timestamp time)
    {
        std::string msg(buf->retriveAllAsString());
        std::cout << "msg : " << msg << std::endl;
        conn->send(msg);
        // conn->shutdown(); // close write --> epoll closeCallBack
    }

    EventLoop *m_loop;
    TcpServer m_server;
};

int main()
{
    EventLoop loop;
    InetAddress addr(8000);

    // 建立acceptor，创建 non-blocking listenfd，bind
    EchoServer server(&loop, addr, "EchoServer");

    // listen， 创建loopthread，将listenfd打包为channel向main_loop注册
    server.start();

    loop.loop();

    return 0;
}