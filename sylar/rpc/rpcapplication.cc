#include "rpcapplication.h"
#include <iostream>
#include <unistd.h>
#include <string>

namespace sylar
{
namespace rpc
{
MprpcConfig MprpcApplication::m_config;

void ShowArgsHelp()
{
    std::cout << "fromit : command -i <configfile>" << std::endl;
}

void MprpcApplication::Init(int argc, char ** argv)
{
    if (argc < 2)
    {
        ShowArgsHelp();
        exit(EXIT_FAILURE);
    }
    int ret = 0;
    std::string config_file;
    while ((ret = getopt(argc, argv, "i:")) != -1)
    {
        switch (ret)
        {
        case 'i' :
            config_file = optarg;
            // std::cout << "Debug : " << config_file << std::endl;
            break;
        case '?' :
            // std::cout << "invalid args !!!" << std::endl;
            ShowArgsHelp();
            exit(EXIT_FAILURE);
            break;
        default:
            // std::cout << "pase args error !!!" << std::endl;
            break;
        }
    }

    // 处理完命令行参数，开始加载配置文件
    m_config.LoadConfigFile(config_file.c_str());

    // std::cout << "rpcserverip : " << m_config.Load("rpcserverip") << std::endl;
}

MprpcApplication& MprpcApplication::GetInstance()
{
    static MprpcApplication app;
    return app;
}

MprpcConfig& MprpcApplication::GetConfig()
{
    return m_config;
}
} // namespace rpc

} // namespace sylar
