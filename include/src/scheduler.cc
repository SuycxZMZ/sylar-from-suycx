#include "scheduler.h"
#include "logger.h"
#include "util.h"

namespace sylar
{
// 当前线程的调度器，同一个调度器下的所有协程共享同一个实例
static thread_local Scheduler *t_scheduler = nullptr;
// 当前线程的调度协程，每个线程独有一份
static thread_local Fiber *t_scheduler_fiber = nullptr;

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name)
{
    if (threads < 1) {
        LOG_FATAL("Scheduler::Scheduler(), threads must be greater than 0");
    }

    m_useCaller = use_caller;
    m_name = name;

    if (use_caller) {
        --threads;
        sylar::Fiber::GetThis();
        t_scheduler = this;

        /**
         * caller线程的主协程不会被线程的调度协程run进行调度，且，线程的调度协程停止时，应该返回caller线程的主协程
         * 在use caller情况下，把caller线程的主协程赞时保存起来，等调度协程结束时，再resume caller协程
        */
       m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));

       sylar::Thread::SetName(m_name);
       t_scheduler_fiber = m_rootFiber.get();
       m_rootThread = sylar::GetThreadId();
       m_threadIds.push_back(m_rootThread);
    } else {
        m_rootThread = -1;
    }
    m_threadCount = threads;
}

Scheduler *Scheduler::GetThis()
{
    return t_scheduler;
}

Fiber *Scheduler::GetMainFiber()
{
    return t_scheduler_fiber;
}

void Scheduler::setThis()
{
    t_scheduler = this;
}

Scheduler::~Scheduler()
{
    if (!m_stopping) {
        LOG_FATAL("Scheduler::~Scheduler(), m_stopping = false");
    }
    if (GetThis() == this) {
        t_scheduler = nullptr;
    }
}

void Scheduler::start()
{
    LOG_DEBUG("Scheduler::start() begin");
    MutexType::Lock lock(m_mutex);
    if (m_stopping) {
        LOG_ERROR("Scheduler::start(), m_stopping = true");
        return;
    }
    if (!m_threads.empty()) {
        LOG_FATAL("Scheduler::start(), m_threads is not empty");
    }

    m_threads.resize(m_threadCount);
    for (size_t i = 0; i < m_threadCount; ++i) {
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this), m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getId());
    }
}

bool Scheduler::stopping()
{
    MutexType::Lock lock(m_mutex);
    return m_stopping && m_tasks.empty() && m_activeThreadCount == 0;
}

void Scheduler::tickle()
{
    LOG_DEBUG("Scheduler::tickle() begin");
}

void Scheduler::idle()
{
    LOG_DEBUG("Scheduler::idle() begin");
    while (!stopping()) {
        sylar::Fiber::GetThis()->yield();
    }
}

void Scheduler::stop()
{
    LOG_DEBUG("Scheduler::stop() begin");
    if (stopping()) {
        return;
    }
    m_stopping = true;

    // 如果 use caller 那只能由caller线程发起stop
    if (m_useCaller) {
        if (GetThis() != this) LOG_FATAL("Scheduler::stop(), GetThis() != this");
    } else {
        if (GetThis() == this) LOG_FATAL("Scheduler::stop(), GetThis() == this");
    }

    for (size_t i = 0; i < m_threadCount; ++i) {
        tickle();
    }

    if (m_rootFiber) {
        tickle();
    }

    // 在use caller 情况下，调度器协程结束时，应该返回caller协程
    if (m_rootFiber) {
        m_rootFiber->resume();
        LOG_DEBUG("m_rootFiber end");
    }

    std::vector<Thread::ptr> thrs;
    {
        MutexType::Lock lock(m_mutex);
        thrs.swap(m_threads);
    }
    for (auto &i : thrs) {
        i->join();
    }
}

void Scheduler::run()
{
    LOG_DEBUG("Scheduler::run() begin");
    setThis();
    if (sylar::GetThreadId() != m_rootThread) {
        t_scheduler_fiber = sylar::Fiber::GetThis().get();
    }

    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
    Fiber::ptr cb_fiber;

    ScheduleTask task;
    while (true) {
        task.reset();
        bool tickle_me = false; // 是否tickle 其他线程进行任务调度
        {
            MutexType::Lock lock(m_mutex);
            auto it = m_tasks.begin();
            // 遍历所有调度任务
            while (it != m_tasks.end()) {
                // 指定了调度线程，但不是在当前线程上调度
                // 标记一下需要通知其他线程调度，然后跳过这个任务，继续轮询下一个
                if (it->thread != -1 && it->thread != sylar::GetThreadId()) {
                    ++it;
                    tickle_me = true;
                }
                // 找到一个未指定线程，或是指定了当前线程的任务
                if (!(it->fiber || it->cb)) {
                    LOG_FATAL("it->fiber == nullptr && it->cb == nullptr");
                }
                if (it->fiber) {
                    // 任务添加到队列时一定是READY状态
                    if (it->fiber->getState() != Fiber::READY) {
                        LOG_FATAL("it->fiber->getState() != Fiber::READY");
                    }
                }
                // 当前调度线程找到一个任务，准备开始调度，将其从任务队列里剔除，活动线程数量加1
                task = *it;
                m_tasks.erase(it++);
                ++m_activeThreadCount;
                break;
            }
            // 当前线程拿完一个任务后，发现任务队列还有剩余，那么tickle一下其他线程
            tickle_me |= (it != m_tasks.end());
        }

        if (tickle_me) {
            tickle();
        }

        if (task.fiber) {
            // resume协程，resume返回时，协程要么执行完毕，要么半路yield
            // 这个任务就算是执行完毕了，活跃线程数量 -1
            task.fiber->resume();
            --m_activeThreadCount;
            task.reset();
        } else if (task.cb) {
            if (cb_fiber) {
                cb_fiber->reset(task.cb);
            } else {
                cb_fiber.reset(new Fiber(task.cb));
            }
            task.reset();
            cb_fiber->resume();
            --m_activeThreadCount;
            cb_fiber.reset();
        } else {
            // 走到当前分支，说明当前任务队列为空，调度idle协程即可
            if (idle_fiber->getState() == Fiber::TERM) {
                // 如果调度器没有调度任务，那么idle协程会不停 resume/yield，不会结束
                // 如果idle协程结束了，那一定是调度器停止了
                LOG_DEBUG("idle_fiber->getState() == Fiber::TERM");
                break;
            }
            ++m_idleThreadCount;
            idle_fiber->resume();
            --m_idleThreadCount;
        }
    }
    LOG_DEBUG("Scheduler::run() exit");
}

} // namespace sylar
