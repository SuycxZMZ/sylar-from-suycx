#include <iostream>
#include <string>
#include "user.pb.h"
#include "rpc/rpcapplication.h"
#include "rpc/rpcprovider.h"

using namespace sylar::rpc;

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

class UserService : public fixbug::UserServiceRpc
{
public:
    bool Login(std::string name, std::string pwd)
    {
        std::cout << "doing local login service" << std::endl;
        std::cout << "name : " << name << std::endl;
        return true;
    }

    bool Register(uint32_t id, std::string name, std::string pwd)
    {
        std::cout << "doing local register service" << std::endl;
        std::cout << "name : " << name << std::endl;
        return true;
    }

    // 业务负责重写 UserServiceRpc的虚函数，框架负责根据指针调用
    void Login(::google::protobuf::RpcController* controller,
                    const ::fixbug::LoginRequest* request,
                    ::fixbug::LoginResponse* response,
                    ::google::protobuf::Closure* done)
    {
        // 框架直接把请求参数反序列化好，直接可以取出数据做本地业务
        std::string name = request->name();
        std::string pwd = request->pwd();

        // 做本地业务
        bool ret = Login(name, pwd);

        // 响应写入 response 
        fixbug::ResultCode * code = response->mutable_result();
        code->set_errcode(0);
        code->set_errmsg("");
        response->set_success(ret);

        // 回调 框架负责 response 的序列化和网络发送
        done->Run();
    }

    void Register(::google::protobuf::RpcController* controller,
                const ::fixbug::RegisterRequst* request,
                ::fixbug::RegisterResponse* response,
                ::google::protobuf::Closure* done)
    {
        uint32_t id = request->id();
        std::string name = request->name();
        std::string pwd = request->pwd();

        bool ret = false;
        if (!(ret = Register(id, name, pwd)))
        {
            std::cout << "register error !!!" << std::endl;
        }

        fixbug::ResultCode * code = response->mutable_result();
        code->set_errcode(0);
        code->set_errmsg("");
        response->set_success(ret);

        done->Run();
    }

};

int main(int argc, char ** argv)
{
    // 框架初始化
    MprpcApplication::Init(argc, argv);

    sylar::IOManager iom(1);

    // 把 UserService 发布到节点上
    RpcProvider provider(&iom);
    provider.NotifyService(new UserService());
    // iom.schedule(std::bind(&RpcProvider::Run, &provider));
    provider.Run();

    return 0;
}