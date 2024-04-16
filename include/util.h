#ifndef UTIL_H
#define UTIL_H


#include <unistd.h>
#include <sys/syscall.h>

namespace sylar
{
/**
 * @brief 获取当前线程的id
*/
pid_t GetThreadId();
} // namespace sylar


#endif // UTIL_H