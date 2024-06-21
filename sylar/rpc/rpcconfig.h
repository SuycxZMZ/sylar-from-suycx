#ifndef RPCCONFIG_H
#define RPCCONFIG_H

#include <unordered_map>
#include <string>

namespace sylar
{
namespace rpc
{
void removeSpaces(std::string& str);

class MprpcConfig
{
public:
    // 加载配置文件
    void LoadConfigFile(const char * config_file);

    // 取ip和端口号
    std::string Load(const std::string & key);
private:
    std::unordered_map<std::string, std::string> m_configMap;
};
} // namespace rpc

} // namespace sylar



#endif