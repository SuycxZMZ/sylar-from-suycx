#ifndef RPCCHANNEL_H
#define RPCCHANNEL_H

#include <google/protobuf/service.h>
#include <google/protobuf/message.h>
#include <google/protobuf/descriptor.h>
#include "rpcheader.pb.h"

#define SEND_RPC_HEADERSIZE 4

namespace sylar
{
namespace rpc
{
/**
 * @brief RPC调用方使用UserServiceRpc_Stub调用远程方法，其实是通过 RpcChannel 调用 CallMethod
*/
class SylarRpcChannel : public google::protobuf::RpcChannel
{
public:
    /**
    * @brief 所有使用 stub
              代理类调用的rpc方法都走到这里，进行序列化和网络发送，接收RPC服务方的response
    * @param method 远程方法
    * @param controller 控制类，用于设置超时时间等
    * @param request 请求参数
    * @param response 返回结果
    * @param done
    */
    virtual void CallMethod(const google::protobuf::MethodDescriptor *method,
                            google::protobuf::RpcController *controller,
                            const google::protobuf::Message *request,
                            google::protobuf::Message *response,
                            google::protobuf::Closure *done) override;
};
} // namespace rpc

} // namespace sylar


#endif