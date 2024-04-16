#pragma once
#include <unistd.h>
#include <sys/syscall.h>

namespace sylar
{
pid_t GetThreadId() { return syscall(SYS_gettid); }
} // namespace sylar
