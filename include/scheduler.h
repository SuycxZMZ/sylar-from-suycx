#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <memory>
#include <mutex>
#include <vector>
#include <list>
#include <atomic>
#include <thread>

#include "fiber.h"
#include "thread.h"
#include "mutex.h"

namespace sylar
{

/**
 * @brief 调度器
 * @details 封装 N-M 协程调度器
 *          内部有一个线程池，支持协程在线程池中切换
*/
class Scheduler
{
public:
    using ptr = std::shared_ptr<Scheduler>;
    using MutexType = sylar::Mutex;

    /**
     * @brief 构造函数
     * @param[in] threads 线程池大小
     * @param[in] use_caller 是否将当前线程作为调度线程
     * @param[in] name 调度器名称
    */
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string &name = "Scheduler");
    virtual ~Scheduler();

    const std::string &getName() const { return m_name; }

    /**
     * @brief 获取当前线程调度器指针
    */
    static Scheduler *GetThis();

    /**
     * @brief 获取当前线程的主协程
    */
    static Fiber *GetMainFiber();

    /**
     * @brief 添加调度任务
     * @tparam FiberOrCb 协程/函数
     * @param[in] fc 协程对象/函数函数指针
     * @param[in] thread 指定在哪个线程上调度，-1表示任意线程
    */
    template <class FiberOrCb>
    void schedule(FiberOrCb fc, int thread = -1)
    {
        bool need_tickle = false;
        {
            MutexType::Lock lock(m_mutex);
            need_tickle = scheduleNoLock(fc, thread);
        }

        if (need_tickle) {
            // 唤醒 idle 协程
            tickle();
        }
    }

    /**
     * @brief 启动调度器
    */
    void start();

    /**
     * @brief 停止调度器
    */
    void stop();

protected:
    /**
     * @brief 通知协程调度器有任务了
    */
    virtual void tickle();

    /**
     * @brief 协程调度函数
    */
    void run();

    /**
     * @brief 无任务时执行idle协程
    */
    virtual void idle();

    /**
     * @brief 返回是否可以停止
    */
    virtual bool stopping();

    /**
     * @brief 设置当前的协程调度器
    */
    void setThis();

    /**
     * @brief 返回是否有空闲线程
     * @details 当前调度协程进入idle时，空闲线程数量加1，从idle协程返回时空闲线程数量减1
    */
    bool hasIdleThreads() {return m_idleThreadCount > 0;}

private:

    /**
     * @brief 添加调度任务，无锁
     * @tparam FiberOrCb 任务调度类型，可以是协程对象或函数指针
     * @param[] fc 
     * @param[] thread 
    */
    template <class FiberOrCb>
    bool scheduleNoLock(FiberOrCb fc, int thread)
    {
        bool need_tickle = m_tasks.empty();
        ScheduleTask task(fc, thread);
        if (task.fiber || task.cb) {
            m_tasks.push_back(task);
        }
        return need_tickle;
    }

private:
    /**
     * @brief 调度任务，协程/函数二选一，可以指定在哪个线程上调度
    */
    struct ScheduleTask
    {
        sylar::Fiber::ptr fiber;
        std::function<void()> cb;
        int thread;

        ScheduleTask(Fiber::ptr f, int thr)
        {
            fiber = f;
            thread = thr;
        }

        ScheduleTask(Fiber::ptr *f, int thr)
        {
            fiber.swap(*f);
            thread = thr;
        }

        ScheduleTask(std::function<void()> f, int thr)
        {
            cb = f;
            thread = thr;
        }
        ScheduleTask() {thread = -1;}

        void reset() 
        {
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }
    };
    

private:
    std::string m_name;
    // 互斥锁
    MutexType m_mutex;
    // 线程池
    std::vector<Thread::ptr> m_threads;
    // 任务队列
    std::list<ScheduleTask> m_tasks;
    // 线程池id数组
    std::vector<int> m_threadIds;
    // 工作线程的数量，不包括use_caller的主线程
    size_t m_threadCount = 0;
    // 活跃线程数
    std::atomic<size_t> m_activeThreadCount{0};
    // idle线程数
    std::atomic<size_t> m_idleThreadCount{0};

    // 是否 use caller
    bool m_useCaller;
    // use caller为true时调度器所在线程的调度协程
    Fiber::ptr m_rootFiber;
    // use caller 为true时调度器所在的线程id
    int m_rootThread = 0;

    // 是否正在停止
    bool m_stopping = false;
};
} // namespace sylar

#endif