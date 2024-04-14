#include <stdio.h>
#include <ucontext.h>
#include <unistd.h>

/**
 * 程序通过getcontext先保存了一个上下文,然后输出"Hello world",
 * 在通过setcontext恢复到getcontext的地方，
 * 重新执行代码，所以导致程序不断的输出”Hello world“
*/
int main(int argc, const char *argv[])
{
    ucontext_t context;
    getcontext(&context);
    puts("Hello world");
    sleep(1);
    setcontext(&context);
    return 0;
}