#include "rpcchannel.h"
#include "rpcapplication.h"
#include "zookeeperutil.h"
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <error.h>

namespace sylar
{
namespace rpc
{
///@brief 所有使用 stub 代理类调用的rpc方法都走到这里，进行序列化和网络发送，接收RPC服务方的 response
///@param method 远程方法
///@param controller 控制类，用于设置超时时间等
///@param request 请求参数
///@param response 返回结果
///@param done 回调函数
void SylarRpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                        google::protobuf::RpcController* controller, 
                        const google::protobuf::Message* request,
                        google::protobuf::Message* response, 
                        google::protobuf::Closure* done)
{
    // 获取服务名和方法名
    const google::protobuf::ServiceDescriptor* service_desc = method->service();
    std::string service_name = service_desc->name();
    std::string method_name = method->name();

    // 序列化 request 请求参数
    uint32_t args_size = 0;
    std::string args_str;
    if (!request->SerializeToString(&args_str))
    {
        controller->SetFailed("Serialize request args_str error !!!");
        // std::cout << "Serialize request args_str error !!!" << std::endl;
        return;
    }

    args_size = args_str.size();

    // set rpcheader
    sylar_rpc::RpcHeader rpcheader;
    rpcheader.set_service_name(service_name);
    rpcheader.set_method_name(method_name);
    rpcheader.set_args_size(args_size);

    // 序列化 rpcheader
    std::string rpcheader_str;
    if (!rpcheader.SerializeToString(&rpcheader_str))
    {
        controller->SetFailed("Serialize request rpcheader error !!!");
        // std::cout << "Serialize request rpcheader error !!!" << std::endl;
        return;
    }
    uint32_t send_all_size = SEND_RPC_HEADERSIZE + rpcheader_str.size() + args_str.size();
    uint32_t rpcheader_size = rpcheader_str.size();

    // 组装 rpc 请求帧
    std::string rpc_send_str;
    // send_all_size
    rpc_send_str += std::string((char*)&send_all_size, 4);
    // rpcheader_size
    rpc_send_str += std::string((char*)&rpcheader_size, 4);
    // rpcheader
    rpc_send_str += rpcheader_str;
    // args
    rpc_send_str += args_str;

    // [DEBUG INFO]
    std::cout << "=======================================" << std::endl;
    std::cout << "header_size : " << rpcheader_size << std::endl;
    std::cout << "service_name : " << service_name << std::endl;
    std::cout << "method_name : " << method_name << std::endl;
    std::cout << "args_size : " << args_size << std::endl;
    std::cout << "args_str : " << args_str << std::endl;
    std::cout << "=======================================" << std::endl;

    // 已经打包好序列化字符流 开始通过网络发送
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd) 
    {
        controller->SetFailed("error create socket : " + std::to_string(errno));
        // std::cout << "error create socket : " << errno << std::endl;
        // exit(EXIT_FAILURE);
        return;
    }

    // std::string ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    // uint16_t port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());
    // 现在从zkserver上读取rpc方法对应的主机
    ZkClient zkcli;
    zkcli.start();
    std::string method_path = "/" + service_name + "/" + method_name;
    std::string host_data = zkcli.GetData(method_path.c_str());
    if ("" == host_data)
    {
        controller->SetFailed(method_path + " is not exist");
        return;
    }
    int idx = host_data.find(":");
    if (-1 == idx)
    {
        controller->SetFailed(method_path + " address is invalid");
        return;
    }
    std::string ip = host_data.substr(0, idx);
    uint16_t port = atoi(host_data.substr(idx+1, host_data.size() - idx - 1).c_str());

    // LOG_INFO("%s:%s:remote: %s, %d", service_name.c_str(), method_name.c_str(), ip.c_str(), port);

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip.c_str());

    // 连接
    if (-1 == connect(clientfd, (sockaddr*)&server_addr, sizeof(server_addr)))
    {
        controller->SetFailed("connect error : " + std::to_string(errno));
        // std::cout << "connect error : " << errno << std::endl;
        close(clientfd);
        // exit(EXIT_FAILURE);
        return;
    }

    // 发送
    if (-1 == send(clientfd, rpc_send_str.c_str(), rpc_send_str.size(), 0))
    {
        controller->SetFailed("send error : " + std::to_string(errno));
        // std::cout << "send error : " << errno << std::endl;
        close(clientfd);
        // exit(EXIT_FAILURE); 
        return;       
    }

    // 接收响应
    char recv_buf[1024] = {0};
    uint32_t recv_size = 0;
    if ((recv_size = recv(clientfd, recv_buf, 1024, 0)) < 0)
    {
        controller->SetFailed("recv error : " + std::to_string(errno));
        // std::cout << "recv error : " << errno << std::endl;
        close(clientfd);
        return;
    }

    // 反序列化 response
    // std::string response_str(recv_buf, 0, recv_size);
    if (!response->ParseFromArray(recv_buf, recv_size))
    // if (!response->ParseFromString(response_str))
    {
        controller->SetFailed("parse response error !!!");
        // std::cout << "parse response error !!! " << std::endl;
        // std::cout << response_str << std::endl;
        close(clientfd);
        return;
    }

    close(clientfd);
}

} // namespace rpc

} // namespace sylar
