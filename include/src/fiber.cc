#include <atomic>
#include <iostream>
#include <assert.h>

#include "logger.h"
#include "fiber.h"
#include "scheduler.h"

namespace sylar
{

// 全局静态变量，用于生成协程ID
static std::atomic<uint64_t> s_fiber_id{0};
// 全局静态变量，用于统计当前协程数
static std::atomic<uint64_t> s_fiber_count{0};

// 线程局部变量，当前线程正在运行的协程
static thread_local Fiber* t_fiber = nullptr;
// 线程局部变量，当前线程的主协程，切换到这个协程，就相当于切换到了主线程中运行，智能指针的形式
static thread_local Fiber::ptr t_thread_fiber = nullptr;

// 协程栈大小，默认128k
static uint32_t g_fiber_stack_size = 128*1024;

// 协程栈内存分配器
class mallocStackAllocator
{
public:
    static void *Alloc(size_t size) { return malloc(size); }
    static void Dealloc(void *vp, size_t size) { return free(vp); }
};

using StackAllocator = mallocStackAllocator;

uint64_t Fiber::GetFiberId()
{
    if (t_fiber) {
        return t_fiber->getId();
    }
    return 0;
}

Fiber::Fiber()
{
    SetThis(this);
    m_state = RUNNING;

    if (getcontext(&m_ctx)) {
        LOG_FATAL("Fiber::Fiber() getcontext error");
        // std::cout << "main fiber getcontext error" << std::endl;
    }

    ++s_fiber_count;
    m_id = s_fiber_id++;
    LOG_DEBUG("Fiber::Fiber() id = %lu", m_id);
    // std::cout << "Fiber::Fiber() main id = " << m_id << std::endl;
}

void Fiber::SetThis(Fiber *fiber)
{
    t_fiber = fiber;
}

/**
 * @brief 获取当前协程
 * @details 同时充当初始化当前线程主协程的作用, 在使用协程之前要调用一下
*/
Fiber::ptr Fiber::GetThis() 
{
    if (t_fiber) {
        return t_fiber->shared_from_this();
    }

    // 如果没有设置主协程，则创建一个协程，并设置为当前线程的主协程
    Fiber::ptr main_fiber(new Fiber);
    t_thread_fiber = main_fiber;
    return t_fiber->shared_from_this();
}

Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) :
    m_id(s_fiber_id++),
    m_cb(cb),
    m_runInScheduler(run_in_scheduler)
{
    ++s_fiber_count;
    m_stacksize = stacksize ? stacksize : g_fiber_stack_size;
    m_stack = StackAllocator::Alloc(m_stacksize);
    if (getcontext(&m_ctx)) {
        LOG_DEBUG("Fiber::Fiber(, , ,) getcontext error");
        // std::cout << "Fiber::Fiber(, , ,) getcontext error" << std::endl;
    }

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);

    LOG_DEBUG("Fiber::Fiber() id = %lu", m_id);
    // std::cout << "Fiber::Fiber() id = " << m_id << std::endl;
}

Fiber::~Fiber()
{
    LOG_DEBUG("Fiber::~Fiber() id = %lu", m_id);
    --s_fiber_count;
    if (m_stack) {
        // 有栈说明是子协程，需要进一步确保子协程是结束状态
        if (m_state != TERM) {
            LOG_FATAL("Fiber::~Fiber() Error id = %lu, m_state = %d", m_id, m_state);
        }

        StackAllocator::Dealloc(m_stack, m_stacksize);
        LOG_DEBUG("dealloc fiber stack id = %lu", m_id);
    }
    else {
        // 没有栈说明是主协程
        if (m_cb || m_state != RUNNING) {
            LOG_FATAL("Fiber::~Fiber() Error id = %lu, m_state = %d", m_id, m_state);
        }

        // 当前协程就是自己
        Fiber *cur = t_fiber;
        if (cur == this) {
            SetThis(nullptr);
        }
    }
}

/**
 * @brief 简化状态管理，只有处于TERM状态的协程才能被重置
*/
void Fiber::reset(std::function<void()> cb)
{
    if (!m_stack || m_state != TERM) {
        LOG_FATAL("Fiber::reset() Error id = %lu, m_state = %d", m_id, m_state);
    }

    m_cb = cb;
    if (getcontext(&m_ctx)) {
        LOG_FATAL("Fiber::reset() getcontext error");
    }

    // 当前上下⽂结束后，下⼀个激活的上下⽂对象的指针，
    // 只在当前上下⽂是由makecontext创建时有效
    m_ctx.uc_link = nullptr;

    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    // makecontext执⾏完后，ucp就与函数func绑定了
    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    m_state = READY;
}

void Fiber::resume()
{
    if (!(m_state != TERM && m_state != RUNNING)) {
        LOG_FATAL("Fiber::resume() Error id = %lu, m_state = %d", m_id, m_state);
    }

    SetThis(this);
    m_state = RUNNING;

    // 如果协程参与调度器调度，则需要和调度器的主协程进行swap，而不是线程的主协程
    if (m_runInScheduler) {
        if (swapcontext(&(Scheduler::GetMainFiber()->m_ctx), &m_ctx)) {
            LOG_FATAL("Fiber::resume() swapcontext error");
        }
    } else {
        if (swapcontext(&(t_thread_fiber->m_ctx), &m_ctx)) {
            LOG_FATAL("Fiber::resume() swapcontext error");
        }
    }
    // if (swapcontext(&(t_thread_fiber->m_ctx), &m_ctx)) {
    //     LOG_FATAL("Fiber::resume() swapcontext error");
    // }
}

void Fiber::yield()
{
    // 协程执行完之后会自动yield一次，用于回到主协程，此时状态已经是结束状态
    if (!(m_state == RUNNING || m_state == TERM)) {
        LOG_FATAL("Fiber::yield() Error id = %lu, m_state = %d", m_id, m_state);
    }

    SetThis(t_thread_fiber.get());
    if (m_state != TERM) {
        m_state = READY;
    }

    if (m_runInScheduler) {
        if (swapcontext(&m_ctx, &(Scheduler::GetMainFiber()->m_ctx))) {
            LOG_FATAL("Fiber::yield() swapcontext error");
        }
    } else {
        if (swapcontext(&m_ctx, &(t_thread_fiber->m_ctx))) {
            LOG_FATAL("Fiber::yield() swapcontext error");
        }
    }

    // if (swapcontext(&m_ctx, &(t_thread_fiber->m_ctx))) {
    //     LOG_FATAL("Fiber::yield() swapcontext error");
    // }
}

void Fiber::MainFunc()
{
    Fiber::ptr cur = GetThis(); // GetThis()的shared_from_this()⽅法让引⽤计数加1
    if (cur == nullptr) {
        LOG_FATAL("Fiber::MainFunc() GetThis() == nullptr");
    }

    cur->m_cb();            // 这⾥真正执⾏协程的⼊⼝函数
    cur->m_cb = nullptr;
    cur->m_state = TERM;

    // 手动让t_fiber的引用计数减1
    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->yield();       // 协程结束时⾃动yield，以回到主协程
}

}