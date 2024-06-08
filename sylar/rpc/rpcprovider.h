#ifndef PROVIDER_H
#define PROVIDER_H

#include "google/protobuf/service.h"
#include <string>
#include <functional>
#include <unordered_map>
#include <google/protobuf/descriptor.h>
#include "../tcp_server.h"

namespace sylar
{
namespace rpc
{
/**
 * @SuycxZMZ
 * @brief RPC提供方
*/
class RpcProvider
{
public:
    RpcProvider();

    ///@brief 框架提供给外部使用的，可以发布rpc调用的接口
    ///@details 发布方要重写 protobuf 生成的 RPC 方法, 通过该函数将服务发布
    ///@param service 传入用户继承自 rpc 方法的子类
    void NotifyService(google::protobuf::Service * service);

    ///@brief 启动rpc服务节点，提供远程rpc调用服务
    ///@details 启动一个 TCP 服务，监听指定的端口，等待远程的 RPC 调用请求
    virtual void ToRun();

    /// @brief 启动调度器，调用 ToRun 开始工作
    void Run();
private:

    ///@brief Service 服务类型信息
    struct ServiceInfo
    {
        // 服务对象
        google::protobuf::Service * m_service;
        // 服务方法映射表 {method_name : method_Desc}
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor *> m_methodMap;
    };
    
    ///@brief 存储注册成功的服务对象和其服务方法的所有信息 {service_name : service_info}
    std::unordered_map<std::string, ServiceInfo> m_serviceInfoMap;

    /// @brief 调用完本地服务后给客户端回发消息的函数，将response转为字符流发出
    /// @param client 客户端来连接sock指针
    /// @param response rpc响应，protobuf负责填写
    void SendRpcResopnse(sylar::Socket::ptr client, google::protobuf::Message* response);

    /// @brief 客户端请求的工具函数
    /// @param client 客户端连接 socket
    void InnerHandleClient(sylar::Socket::ptr client);

    /// @brief io调度器
    sylar::IOManager m_iom;

    bool m_isrunning;
private:
    /// @brief RpcTcpServer类负责底层网络收发
    class RpcTcpServer : public sylar::TcpServer {
    public:
        /// @brief RpcTcpServer构造函数
        /// @param _rpcprovider 绑定的 rpcprovider，one provider per server
        RpcTcpServer(RpcProvider* _rpcprovider);
        virtual void handleClient(sylar::Socket::ptr client) override;
    private:
        RpcProvider* m_rpcprovider = nullptr;
    };
};
} // namespace rpc

} // namespace sylar



#endif