#include <iostream>
#include "user.pb.h"
#include "rpc/rpcapplication.h"
#include "rpc/rpcchannel.h"

using namespace sylar::rpc;

int main(int argc, char ** argv)
{
    // 初始化
    MprpcApplication::Init(argc, argv);

    // 调用远程rpc方法代理类
    fixbug::UserServiceRpc_Stub stub(new SylarRpcChannel());

    // 业务
    fixbug::LoginRequest request;
    fixbug::LoginResponse response;
    request.set_name("zhang san");
    request.set_pwd("123456");

    fixbug::RegisterRequst reg_request;
    fixbug::RegisterResponse reg_response;
    reg_request.set_id(999);
    reg_request.set_name("li si");
    reg_request.set_pwd("666666");

    // 通过代理调用方法 同步调用 --> MpRpcChannel::CallMethod
    // MpRpcChannel 继承 google::protobuf::RpcChannel 并重写 CallMethod
    stub.Login(nullptr, &request, &response, nullptr);
    
    // 调用完成
    if (0 == response.result().errcode())
    {
        std::cout << "rpc login response : " << response.success() << std::endl;
    }
    else
    {
        std::cout << "rpc login response error : " << response.result().errmsg() << std::endl;
    }

    stub.Register(nullptr, &reg_request, &reg_response, nullptr);
    if (0 == reg_response.result().errcode())
    {
        std::cout << "rpc register response : " << reg_response.success() << std::endl;
    }
    else
    {
        std::cout << "rpc register response error : " << reg_response.result().errmsg() << std::endl;
    }


    sleep(10);
    stub.Login(nullptr, &request, &response, nullptr);
    stub.Register(nullptr, &reg_request, &reg_response, nullptr);

    return 0;
}