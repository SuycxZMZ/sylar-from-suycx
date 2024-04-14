#include <ucontext.h>
#include <stdio.h>

void func1(void *arg)
{
    puts("1");
    puts("11");
    puts("111");
    puts("1111");
}

/**
 * 实现用户线程的过程是：
 * 我们首先调用getcontext获得当前上下文
 * 修改当前上下文ucontext_t来指定新的上下文，如指定栈空间极其大小，设置用户线程执行完后返回的后继上下文（即主函数的上下文）等
 * 调用makecontext创建上下文，并指定用户线程中要执行的函数
 * 切换到用户线程上下文去执行用户线程（如果设置的后继上下文为主函数，则用户线程执行完后会自动返回主函数）。
 */
void context_test()
{
    char stack[1024 * 128];
    ucontext_t child, main;

    getcontext(&child);                     // 获取当前上下文
    child.uc_stack.ss_sp = stack;           // 指定栈空间
    child.uc_stack.ss_size = sizeof(stack); // 指定栈空间大小
    child.uc_stack.ss_flags = 0;            // 
    child.uc_link = &main; // 设置后继上下文

    makecontext(&child, (void (*)(void))func1, 0); // 修改上下文指向func1函数

    swapcontext(&main, &child); // 切换到child上下文，保存当前上下文到main
    
    puts("main");               // 如果设置了后继上下文，func1函数指向完后会返回此处
}

int main()
{
    context_test();

    return 0;
}