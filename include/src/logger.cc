#include <iostream>

#include "logger.h"
#include "timestamp.h"

// 获取日志单例
Logger &Logger::instance()
{
    static Logger log;
    return log;
}

// 设置日志级别
void Logger::setLogLevel(int level)
{
    m_level = level;
}

// 写日志
void Logger::log(std::string log_msg)
{
    switch (m_level)
    {
    case INFO:
        std::cout << "[INFO] ";
        break;

    case ERROR:
        std::cout << "[ERROR]";
        break;
    case DEBUG:
        std::cout << "[DEBUG]";
        break;
    case FATAL:
        std::cout << "[FATAL]";
        break;
    default:
        break;
    }

    // 打印时间
    std::cout << Timestamp::now().toString() << " " << log_msg << std::endl;
}