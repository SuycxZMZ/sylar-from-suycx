#ifndef RPCAPPLICATION_H
#define RPCAPPLICATION_H

#include "rpcconfig.h"
#include "rpcchannel.h"
#include "rpccontroller.h"

namespace sylar
{
namespace rpc
{
// Mprpc 基础类，设计为单例
class MprpcApplication
{
public:
    static void Init(int argc, char ** argv);
    static MprpcApplication& GetInstance();
    static MprpcConfig& GetConfig();
private:
    MprpcApplication() {}
    MprpcApplication(const MprpcApplication &) = delete;
    MprpcApplication(MprpcApplication &&) = delete;

    static MprpcConfig m_config;
};
} // namespace rpc

} // namespace sylar



#endif