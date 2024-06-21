#include <iostream>
#include <chrono>
#include "sylar/sylar.h"

constexpr int ITERATIONS = 10000000;

void coroutine2();
void coroutine1() {
    sylar::Fiber::ptr fiber(new sylar::Fiber(coroutine2, 0, false));
    for (int i = 0; i < ITERATIONS; ++i) {
        fiber->resume();
    }
}

void coroutine2() {
    for (int i = 0; i < ITERATIONS; ++i) {
        sylar::Fiber::GetThis()->yield();
    }
}

int main() {
    auto start = std::chrono::high_resolution_clock::now();

    sylar::Fiber::ptr fiber(new sylar::Fiber(coroutine1, 0, false));
    fiber->resume();

    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> duration = end - start;
    double timePerSwitch = (duration.count() / (2 * ITERATIONS)) * 1e6; // 转换为微秒

    std::cout << "Total time: " << duration.count() << " seconds" << std::endl;
    std::cout << "Time per coroutine switch: " << timePerSwitch << " microseconds" << std::endl;

    return 0;
}
