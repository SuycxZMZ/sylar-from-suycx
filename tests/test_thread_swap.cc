#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

constexpr int SWITCH_COUNT = 1000000;
int switch_count = 0;
std::mutex mtx;
std::condition_variable cv;
bool flag = false;

void thread_func() {
    while (true) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [] { return flag; });
        flag = false;
        if (++switch_count >= SWITCH_COUNT) {
            cv.notify_one();
            break;
        }
        cv.notify_one();
    }
}

int main() {
    std::thread t(thread_func);

    auto start = std::chrono::high_resolution_clock::now();
    while (true) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [] { return !flag; });
        flag = true;
        if (++switch_count >= SWITCH_COUNT) {
            cv.notify_one();
            break;
        }
        cv.notify_one();
    }
    auto end = std::chrono::high_resolution_clock::now();

    t.join();

    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Total time for " << SWITCH_COUNT << " thread switches: " << elapsed.count() << " seconds" << std::endl;
    std::cout << "Average time per switch: " << (elapsed.count() / SWITCH_COUNT) << " seconds" << std::endl;

    return 0;
}
