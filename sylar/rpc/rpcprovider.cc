#include "rpcprovider.h"
#include "mprpcapplication.h"
#include "rpcheader.pb.h"
#include "zookeeperutil.h"

namespace sylar
{
namespace rpc
{
void RpcProvider::NotifyService(google::protobuf::Service * service)
{
    ServiceInfo service_info;
    // 获取服务描述符
    const google::protobuf::ServiceDescriptor * serviceDesc = service->GetDescriptor();
    // 获取服务名字
    const std::string serviceName = serviceDesc->name();

    // 获取服务方法的数量
    int methodCnt = serviceDesc->method_count();

    for (int i = 0; i < methodCnt; ++i)
    {
        // 获取服务对象指定方法的描述
        const google::protobuf::MethodDescriptor * methodPtr = serviceDesc->method(i);
        std::string method_name = methodPtr->name();
        service_info.m_methodMap.emplace(method_name, methodPtr);
    }
    service_info.m_service = service;
    m_serviceInfoMap.emplace(serviceName, service_info);
}

void RpcProvider::Run()
{
    std::string ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    uint16_t port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());
    tinymuduo::InetAddress address(port, ip);

    // 创建 TcpServer
    tinymuduo::TcpServer server(&m_eventLoop, address, "RpcServer");

    // 绑定事件回调
    server.setConnCallBack(std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
    server.setMsgCallBack(std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    // 设置server线程数量
    server.setThreadNum(4);

    // 注册到 zk 上， rpcclient可以从zkserver上发现服务
    ZkClient zkcli;
    zkcli.start();
    // 设置service_name为永久节点 method_name为临时节点
    for (auto & sp : m_serviceInfoMap)
    {
        std::string service_path = "/" + sp.first;
        zkcli.create(service_path.c_str(), nullptr, 0);
        for (auto & mp : sp.second.m_methodMap)
        {
            std::string method_path = service_path + "/" + mp.first;
            char method_path_data[128] = {0};
            sprintf(method_path_data, "%s:%d", ip.c_str(), port);
            zkcli.create(method_path.c_str(), method_path_data, strlen(method_path_data), ZOO_EPHEMERAL);
        }
    }

    // 启动server
    server.start();
    m_eventLoop.loop();
}

void RpcProvider::OnConnection(const tinymuduo::TcpConnectionPtr &conn)
{
    if (!conn->connected())
    {
        conn->shutdown();
    }
}

void RpcProvider::OnMessage(const tinymuduo::TcpConnectionPtr &conn, 
                            tinymuduo::Buffer * buffer, 
                            tinymuduo::Timestamp time)
{
    std::string recv_buf = buffer->retriveAllAsString();

    // 读取头部大小
    uint32_t header_size = 0;
    recv_buf.copy((char*)&header_size, 4, 0);

    // 读取头部原始字符流
    std::string header_str = recv_buf.substr(4, header_size);

    // 反序列化
    mprpc::RpcHeader rpcHeader;
    std::string service_name;
    std::string method_name;
    uint32_t args_size;
    if (rpcHeader.ParseFromString(header_str))
    {
        service_name = rpcHeader.service_name();
        method_name = rpcHeader.method_name();
        args_size = rpcHeader.args_size();
    }
    else
    {
        // 反序列化失败
        std::cout << "rpcHeader header_str : " << header_str << " pase error !!!" << std::endl;
        return; 
    }
    // 取出参数
    std::string args_str = recv_buf.substr(4 + header_size, args_size);

    // 调试信息
    std::cout << "=======================================" << std::endl;
    std::cout << "header_size : " << header_size << std::endl;
    std::cout << "service_name : " << service_name << std::endl;
    std::cout << "method_name : " << method_name << std::endl;
    std::cout << "args_size : " << args_size << std::endl;
    std::cout << "args_str : " << args_str << std::endl;
    std::cout << "=======================================" << std::endl;

    // 获取service对象和method对象
    auto it = m_serviceInfoMap.find(service_name);
    if (m_serviceInfoMap.end() == it)
    {
        // 找不到对应的服务对象
        std::cout << service_name << " is not exist !!! " << std::endl;
        return;
    }
    auto mit = it->second.m_methodMap.find(method_name);
    if (it->second.m_methodMap.end() == mit)
    {
        std::cout << service_name << " method_name: " << method_name << " is not exist !!! " << std::endl;
        return;
    }

    google::protobuf::Service * service = it->second.m_service;
    const google::protobuf::MethodDescriptor * method = mit->second;

    // 生成rpc方法的请求request 和 response响应参数
    google::protobuf::Message * request = service->GetRequestPrototype(method).New();
    if (!request->ParseFromString(args_str))
    {
        std::cout << "request parse error : " << args_str << std::endl;
        return;
    }
    google::protobuf::Message * response = service->GetResponsePrototype(method).New();

    // 绑定Closure回调
    google::protobuf::Closure * done = 
        google::protobuf::NewCallback<RpcProvider, 
                                  const tinymuduo::TcpConnectionPtr &, 
                                  google::protobuf::Message *>
                                  (this, &RpcProvider::SendRpcResponse, 
                                   conn, response);

    // 根据远端rpc请求，调用本地发布的方法，这里走的是 protobuf 生成的 rpcservice 中的 CallMethod 
    // CallMethod 调用具体的 rpcservice 方法，
    // 具体的 rpcservice 方法 用户的服务发布代码中被重写
    service->CallMethod(method, nullptr, request, response, done);
}

void RpcProvider::SendRpcResponse(const tinymuduo::TcpConnectionPtr & conn, google::protobuf::Message * response)
{
    // 把rpc响应序列化为字符流发送给远程调用方
    std::string response_str;
    if (response->SerializeToString(&response_str))
    {
        // 序列化成功之后把执行结果返回给调用方
        conn->send(response_str);
    }
    else
    {
        std::cout << "response SerializeToString error !!! " << std::endl;
    }
    // 模拟http短链接，发送完服务器主动断开
    conn->shutdown();
}

void RpcProvider::RpcTcpServer::handleClient(sylar::Socket::ptr client) {
    
}

} // namespace rpc

} // namespace sylar


