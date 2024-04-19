#include "util.h"

namespace sylar
{
pid_t GetThreadId() { return syscall(SYS_gettid); }

uint64_t GetElapsedMS() 
{
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
} // namespace sylar
