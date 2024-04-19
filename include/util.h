#ifndef UTIL_H
#define UTIL_H

#include <cstdint>
#include <unistd.h>
#include <sys/syscall.h>
#include <time.h>

namespace sylar
{
/**
 * @brief 获取当前线程的id
*/
pid_t GetThreadId();

/**
 * @brief 获取当前时间
*/
uint64_t GetElapsedMS();
} // namespace sylar


#endif // UTIL_H