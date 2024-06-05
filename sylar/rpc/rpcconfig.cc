#include "rpcconfig.h"

#include <iostream>
#include <string>

namespace sylar
{
namespace rpc
{
void MprpcConfig::LoadConfigFile(const char * config_file)
{
    FILE * pf = fopen(config_file, "r");
    if (nullptr == pf)
    {
        std::cout << config_file << " is not exist !!!" << std::endl;
        exit(EXIT_FAILURE);
    }

    while (!feof(pf))
    {
        char buf[512];
        fgets(buf, 512, pf);

        // 去掉多余的空格
        std::string str_buf(buf);
        removeSpaces(str_buf);
        if (str_buf[0] == '#' || str_buf.empty())
        {
            continue;
        }

        // 解析配置项
        int idx = str_buf.find('=');
        if (-1 == idx)
        {
            continue;
        }
        
        std::string key = str_buf.substr(0, idx);
        std::string value = str_buf.substr(idx + 1, str_buf.length() - idx - 1);
        if (value.back() == '\n') value.resize(value.length() - 1);

        m_configMap.emplace(key, value);
    }
}

// 获取配置项 
std::string MprpcConfig::Load(const std::string & key)
{
    auto it = m_configMap.find(key);
    if (m_configMap.end() == it) return "";
    return it->second;
}

void removeSpaces(std::string& str) 
{
    int readPos = 0; 
    int writePos = 0; 

    // 遍历原始字符串
    while (readPos < str.length()) 
    {
        // 如果当前字符不是空格，将其复制到处理后的字符串中
        if (str[readPos] != ' ') 
        {
            str[writePos++] = str[readPos];
        }
        readPos++; // 移动读指针
    }

    // 将处理后的字符串的末尾设置为'\0'，以表示字符串的结束
    str.resize(writePos);
}
} // namespace rpc

} // namespace sylar
