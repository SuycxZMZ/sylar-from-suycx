#ifndef FIBER_H
#define FIBER_H

#include <functional>
#include <memory>
#include <ucontext.h>

namespace sylar
{
/// @brief 协程类
class Fiber : public std::enable_shared_from_this<Fiber>
{
public:
    using ptr = std::shared_ptr<Fiber>;
    /// @brief  协程状态
    /// @details READY-->RUNNING, RUNNING-->READY, RUNNING-->TERM
    enum State
    {
        // 就绪态，刚刚创建或者yield之后的状态
        READY,
        // 运行态，resume之后的状态
        RUNNING,
        // 结束态，协程的回调函数执行完成之后的状态
        TERM
    };
private:
    /**
     * @brief 构造函数
     * @attention 无参构造函数只用于创建线程的第一个协程，也就是线程主函数对应的协程，
     * 这个协程只能由GetThis()方法调用，所以定义成私有方法
     */
    Fiber();
public:
    /**
     * @brief 构造函数, 用于创建用户协程
     * @param[in] cb 协程回调函数
     * @param[in] stacksize 协程栈大小，默认0，表示使用系统默认栈大小
     * @param[in] run_in_scheduler 是否参与协程调度器调度，默认true
    */
    Fiber(std::function<void()> cb, size_t stacksize = 0, bool run_in_scheduler = true);
    ~Fiber();

    /**
     * @brief 重置协程状态和入口函数，复用栈空间，不用重新创建栈
     * @param[in] cb 
    */
    void reset(std::function<void()> cb);

    /**
     * @brief 将当前协程切换到执行状态
     * @details 当前协程和正在执行的协程进行交换，前者状态变为RUNNING 后者变为READY
    */
    void resume();

    /**
     * @brief 当前协程让出执行权
     * @details 当前协程与上次resume时退到后台的协程进行交换，前者变为READY, 后者变为RUNNING
    */
    void yield();

    // 获取当前协程的id
    uint64_t getId() const { return m_id; }
    // 获取当前协程的状态
    State getState() const { return m_state; }
public:
    // 设置当前正在运行的协程，即t_fiber
    static void SetThis(Fiber *f);

    /**
     * @brief 返回当前线程正在执行的协程
     * @details 如果当前线程还未创建协程，则创建线程的第一个协程，
     * 且该协程为当前线程的主协程，其他协程都通过这个协程来调度，也就是说，其他协程
     * 结束时,都要切回到主协程，由主协程重新选择新的协程进行resume
     * @attention 线程如果要创建协程，那么应该首先执行一下Fiber::GetThis()操作，以初始化主函数协程
     */    
    static Fiber::ptr GetThis();

    // 获取总协程数
    static uint64_t TotalFibers();

    // 协程入口函数
    static void MainFunc();

    // 获取当前协程id
    static uint64_t GetFiberId();
private:
    // 协程id
    uint64_t m_id = 0;
    // 协程栈大小
    uint32_t m_stacksize = 0;
    // 协程状态
    State m_state = READY;
    // 协程上下文
    ucontext_t m_ctx;
    // 协程栈地址
    void *m_stack = nullptr;
    // 协程入口函数
    std::function<void()> m_cb;
    // 本协程是否参与协程调度器调度
    bool m_runInScheduler;
};
}

#endif