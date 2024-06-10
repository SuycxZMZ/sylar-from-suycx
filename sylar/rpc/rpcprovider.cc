#include "rpcprovider.h"
#include "rpcapplication.h"
#include "rpcheader.pb.h"
#include "zookeeperutil.h"
#include "../macro.h"
#include <vector>
#include <string>
#include <memory>

namespace sylar
{
namespace rpc
{

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

RpcProvider::RpcProvider(sylar::IOManager* _iom) : m_isrunning(false), m_iom(_iom) {
}

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
        SYLAR_LOG_INFO(g_logger) << method_name << ":" << methodPtr;
    }
    service_info.m_service = service;
    m_serviceInfoMap.emplace(serviceName, service_info);
}

void RpcProvider::InnerStart() {
    m_isrunning = true;
    // 创建server，绑定server监听端口
    std::string ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    std::string port = MprpcApplication::GetInstance().GetConfig().Load("rpcserverport");

    auto addr = sylar::Address::LookupAny(ip + ":" + port);
    SYLAR_ASSERT(addr);
    std::vector<sylar::Address::ptr> addrs;
    addrs.push_back(addr);
    std::vector<sylar::Address::ptr> fails;
    while(!m_TcpServer->bind(addrs, fails)) {
        sleep(2);
    }
    SYLAR_LOG_INFO(g_logger) << "bind success, " << m_TcpServer->toString();
    
    // 注册到 zk 上， rpcclient可以从zkserver上发现服务
    // ZkClient zkcli;
    m_zkcli->start();
    // 设置service_name为永久节点 method_name为临时节点
    for (auto & sp : m_serviceInfoMap)
    {
        std::string service_path = "/" + sp.first;
        m_zkcli->create(service_path.c_str(), nullptr, 0);
        for (auto & mp : sp.second.m_methodMap)
        {
            std::string method_path = service_path + "/" + mp.first;
            char method_path_data[128] = {0};
            sprintf(method_path_data, "%s:%s", ip.c_str(), port.c_str());
            SYLAR_LOG_INFO(g_logger) << method_path << ":" << method_path_data << " start create !!!";
            m_zkcli->create(method_path.c_str(), method_path_data, strlen(method_path_data), ZOO_EPHEMERAL);
        }
    }

    m_TcpServer->start();
    // zkClient保活
    // while (m_isrunning) {
    //     std::cout << "------------- tick \n";
    //     sleep(5);
    // }   
}

void RpcProvider::Run() {
    m_iom->schedule(std::bind(&RpcProvider::InnerStart, this));
}

RpcTcpServer::RpcTcpServer(RpcProvider* _rpcprovider) : m_rpcprovider(_rpcprovider) {}

void RpcProvider::InnerHandleClient(sylar::Socket::ptr client) {
    SYLAR_LOG_INFO(g_logger) << "new msg";
    std::string recv_buf;
    recv_buf.resize(4096);
    if (client->recv(&recv_buf[0], recv_buf.size()) <= 0) {
        SYLAR_LOG_ERROR(g_logger) << "recv rpcrequest error";
    }

    // 读取头部大小
    uint32_t header_size = 0;
    recv_buf.copy((char*)&header_size, 4, 0);
    // 读取头部
    std::string header_str = recv_buf.substr(4, header_size);

    // 反序列化
    sylar_rpc::RpcHeader rpcHeader;
    std::string service_name;
    std::string method_name;
    uint32_t args_size;
    if (rpcHeader.ParseFromString(header_str)) {
        service_name = rpcHeader.service_name();
        method_name = rpcHeader.method_name();
        args_size = rpcHeader.args_size();
    } else { // 反序列化失败
        SYLAR_LOG_ERROR(g_logger) << "rpcHeader header_str : " << header_str << " pase error !!!";
        return;
    }

    // 取参数
    std::string args_str = recv_buf.substr(4 + header_size, args_size);
    // [DEBUG INFO]
    SYLAR_LOG_INFO(g_logger) << "\n-----------------------------\n" 
                << "header_size : " << header_size << "\n"
                << "service_name : " << service_name << "\n"
                << "method_name : " << method_name << "\n"
                << "args_size : " << args_size << "\n"
                << "args_str : " << args_str << "\n"
                << "-----------------------------\n" ;
    
    // 获取service对象和method对象

    auto it = m_serviceInfoMap.find(service_name);
    if (m_serviceInfoMap.end() == it) {
        SYLAR_LOG_ERROR(g_logger) << service_name << " is not exist !!!";
        return;
    }
    auto mit = it->second.m_methodMap.find(method_name);
    if (it->second.m_methodMap.end() == mit) {
        SYLAR_LOG_ERROR(g_logger) << method_name << " is not exist !!!";
        return;
    }

    // 取出服务对象
    google::protobuf::Service* service = it->second.m_service;
    const google::protobuf::MethodDescriptor * method = mit->second;
    // 生成rpc方法的请求request 和 response响应参数
    google::protobuf::Message * request = service->GetRequestPrototype(method).New();
    if (!request->ParseFromString(args_str))
    {
        SYLAR_LOG_ERROR(g_logger) << "request parse error : " << args_str;
        return;
    }   
    google::protobuf::Message * response = service->GetResponsePrototype(method).New();
    google::protobuf::Closure* done = 
        google::protobuf::NewCallback<sylar::rpc::RpcProvider, sylar::Socket::ptr, 
                                      google::protobuf::Message*> 
        (this, &RpcProvider::SendRpcResopnse, client, response);
    service->CallMethod(method, nullptr, request, response, done);
}

void RpcTcpServer::handleClient(sylar::Socket::ptr client) {
    if (nullptr == m_rpcprovider) {
        SYLAR_LOG_FATAL(g_logger) << "server not correct init !!!";
    }
    m_rpcprovider->InnerHandleClient(client);
}

void RpcProvider::SendRpcResopnse(sylar::Socket::ptr client, google::protobuf::Message* response) {
    // 把rpc响应序列化为字符流发送给远程调用方
    std::string response_str;
    if (response->SerializeToString(&response_str)) {
        if (client->send(&response_str[0], response_str.size()) <= 0) {
            SYLAR_LOG_ERROR(g_logger) << "send rpcresponse error";
        }
    } else {
        SYLAR_LOG_ERROR(g_logger) << "response SerializeToString error !!! ";
    }
    // client->close();
}

RpcProvider::~RpcProvider() {
    m_isrunning = false;
}

} // namespace rpc

} // namespace sylar


