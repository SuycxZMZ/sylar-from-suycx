#ifndef PROVIDER_H
#define PROVIDER_H

#include "google/protobuf/service.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <google/protobuf/descriptor.h>
#include "../tcp_server.h"
#include "zookeeperutil.h"

#define RECV_RPC_HEADERSIZE 4

namespace sylar
{
namespace rpc
{

// 前向声明
class RpcProvider;

/**
 * @brief RpcTcpServer类负责底层网络收发
 */
class RpcTcpServer : public sylar::TcpServer {
public:
    using ptr = std::shared_ptr<RpcTcpServer>;
    RpcTcpServer(RpcProvider *_rpcprovider, sylar::IOManager *io_woker,
                 sylar::IOManager *accept_worker);
    void handleClient(sylar::Socket::ptr client) override;

  protected:
    // 带一个provider指针
    RpcProvider* m_rpcprovider;
};

/**
 * @brief RPC提供方
*/
class RpcProvider
{
public:
    /**
    * @brief 框架提供给外部使用的，可以发布rpc调用的接口
    * @details 发布方要重写 protobuf 生成的 RPC 方法, 通过该函数将服务发布
    * @param service 传入用户继承自 rpc 方法的子类
    */
    void NotifyService(google::protobuf::Service *service);

    /**
    * @brief 内部开启 server
    * @attention 继承者可以重写，框架会自动调用
    */
    virtual void InnerStart();

    /**
    * @brief 开启server并保持心跳
    */
    virtual void Run();

    /**
    * @brief 客户端请求的工具函数
    * @param client 客户端连接 socket
    */
    void InnerHandleClient(sylar::Socket::ptr client);

    /**
    * @brief Construct a new Rpc Provider object
    * @param iom 传入的调度器指针
    */
    explicit RpcProvider(sylar::IOManager::ptr iom);

    /**
    * @brief Destroy the Rpc Provider object
    * @attention 写为virtual，防止内存泄漏
    */
    virtual ~RpcProvider();
protected:
    // Service 服务类型信息
    struct ServiceInfo
    {
        // 服务对象
        google::protobuf::Service * m_service;
        // 服务方法映射表 {method_name : method_Desc}
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor *> m_methodMap;
    };
    
    /**
     * @brief 调用完本地服务后给客户端回发消息的函数，将response转为字符流发出
     * @param client 客户端来连接sock指针
     * @param response rpc响应，protobuf负责填写
     */
    void SendRpcResopnse(sylar::Socket::ptr client, google::protobuf::Message* response);

    // 存储注册成功的服务对象和其服务方法的所有信息 {service_name : service_info}
    std::unordered_map<std::string, ServiceInfo> m_serviceInfoMap;
    // 是否已经开启服务
    bool m_isRunning;
    // 底层tcpserver指针
    RpcTcpServer::ptr m_tcpServer;
    // 调度器指针
    sylar::IOManager::ptr m_iom;
    // zk客户端
    std::shared_ptr<ZkClient> m_zkcli;
};



} // namespace rpc

} // namespace sylar



#endif