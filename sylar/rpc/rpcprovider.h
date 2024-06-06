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
    ///@brief 框架提供给外部使用的，可以发布rpc调用的接口
    ///@details 发布方要重写 protobuf 生成的 RPC 方法, 通过该函数将服务发布
    ///@param service 传入用户继承自 rpc 方法的子类
    void NotifyService(google::protobuf::Service * service);

    ///@brief 启动rpc服务节点，提供远程rpc调用服务
    ///@details 启动一个 TCP 服务，监听指定的端口，等待远程的 RPC 调用请求
    ///         1. 从MprpcApplication获取ip和端口号
    ///         2. 创建TcpServer对象，绑定回调函数
    ///         3. 往zk上创建节点，注册服务信息
    ///         4. 启动事件循环，开始工作
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

    class RpcTcpServer : public sylar::TcpServer {
        virtual void handleClient(sylar::Socket::ptr client) override;
    };

    sylar::IOManager m_iom;
};
} // namespace rpc

} // namespace sylar



#endif