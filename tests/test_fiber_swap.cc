#include <iostream>
#include <ucontext.h>
#include <chrono>

constexpr int STACK_SIZE = 1024 * 64;
constexpr int SWITCH_COUNT = 1000000;

ucontext_t context1, main_context;
char stack1[STACK_SIZE];
int switch_count = 0;

void coroutine() {
    while (switch_count < SWITCH_COUNT) {
        ++switch_count;
        swapcontext(&context1, &main_context);
    }
}

int main() {
    getcontext(&context1);
    context1.uc_stack.ss_sp = stack1;
    context1.uc_stack.ss_size = sizeof(stack1);
    context1.uc_link = &main_context;
    makecontext(&context1, coroutine, 0);

    auto start = std::chrono::high_resolution_clock::now();
    while (switch_count < SWITCH_COUNT) {
        swapcontext(&main_context, &context1);
    }
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Total time for " << SWITCH_COUNT << " switches: " << elapsed.count() << " seconds" << std::endl;
    std::cout << "Average time per switch: " << (elapsed.count() / SWITCH_COUNT) << " seconds" << std::endl;

    return 0;
}
