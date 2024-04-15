#ifndef LOGGER_H
#define LOGGER_H

#include <string>

#include "noncopyable.h"

// #define MUDUODEBUG 0

// log宏 LOG_INFO(formatstr, arg1, arg2, ...)
#define LOG_INFO(logMsgFormat, ...)                       \
    do                                                    \
    {                                                     \
        Logger &logger = Logger::instance();              \
        logger.setLogLevel(INFO);                         \
        char buf[1024];                                   \
        snprintf(buf, 1024, logMsgFormat, ##__VA_ARGS__); \
        logger.log(std::string(buf) + "\n");              \
    } while (0)

#define LOG_ERROR(logMsgFormat, ...)                      \
    do                                                    \
    {                                                     \
        Logger &logger = Logger::instance();              \
        logger.setLogLevel(ERROR);                        \
        char buf[1024];                                   \
        snprintf(buf, 1024, logMsgFormat, ##__VA_ARGS__); \
        logger.log(std::string(buf) + "\n");              \
    } while (0)
#if MUDUODEBUG
#define LOG_DEBUG(logMsgFormat, ...)                      \
    do                                                    \
    {                                                     \
        Logger &logger = Logger::instance();              \
        logger.setLogLevel(DEBUG);                        \
        char buf[1024];                                   \
        snprintf(buf, 1024, logMsgFormat, ##__VA_ARGS__); \
        logger.log(std::string(buf) + "\n");              \
    } while (0)
#else
#define LOG_DEBUG(logMsgFormat, ...)
#endif

#define LOG_FATAL(logMsgFormat, ...)                      \
    do                                                    \
    {                                                     \
        Logger &logger = Logger::instance();              \
        logger.setLogLevel(FATAL);                        \
        char buf[1024];                                   \
        snprintf(buf, 1024, logMsgFormat, ##__VA_ARGS__); \
        logger.log(std::string(buf) + "\n");              \
        exit(EXIT_FAILURE);                               \
    } while (0)

// 日志级别
enum loglevel
{
    INFO,
    ERROR,
    DEBUG,
    FATAL
};

// 日志类，单例
class Logger : noncopyable
{
public:
    // 获取日志单例
    static Logger &instance();
    // 设置日志级别
    void setLogLevel(int level);
    // 写日志
    void log(std::string log_msg);

private:
    // 级别
    int m_level;
    Logger() {}
};

#endif