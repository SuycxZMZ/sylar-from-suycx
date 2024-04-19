#include <sys/epoll.h>
#include <unistd.h>
#include <strings.h>
#include <fcntl.h>
#include <errno.h>
#include <algorithm>

#include "logger.h"
#include "iomanager.h"

namespace sylar
{
IOManager::IOManager(size_t threads, bool use_caller, const std::string &name) :
    Scheduler(threads, use_caller, name)
{
    m_epfd = epoll_create(5000);
    if (m_epfd < 1) {
        LOG_FATAL("%s:%d:%s: epoll_create error", __FILE__, __LINE__, __FUNCTION__);
    }

    int rt = pipe(m_tickleFds);
    if (rt) {
        LOG_FATAL("%s:%d:%s pipe create error", __FILE__, __LINE__, __FUNCTION__);
    }

    // 关注 pipe 读句柄的可读事件，用于tickle协程
    epoll_event event;
    bzero(&event, sizeof(event));
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = m_tickleFds[0];

    // 非阻塞 + 边缘触发
    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
    if (rt) {
        LOG_FATAL("%s:%d:%s fcntl error", __FILE__, __LINE__, __FUNCTION__);
    }

    // 如果管道可读，则 idle 中的epoll_wait返回
    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
    if (rt) {
        LOG_FATAL("%s:%d:%s epoll_ctl error", __FILE__, __LINE__, __FUNCTION__);
    }

    contextResize(32);

    start();
}

/**
 * @brief 通知调度器有任务要调度
 * @details 写pipe 让idle协程从epoll_wait中返回,待idle协程yield后,Scheduler::run() 就可以调度其他任务
 *          如果当前没有空闲调度线程，那就没有必要发通知
*/
void IOManager::tickle()
{
    LOG_DEBUG("tickle()");
    if (!hasIdleThreads()) {
        return;
    }

    int rt = write(m_tickleFds[1], "T", 1);
    if (rt != 1) {
        LOG_FATAL("tickle() write error");
    }
}

/**
 * 调度器无调度任务时会阻塞在idle协程上，对IO调度器而言，idle状态应该关注两件事，一是有没有新的调度任务，对应Schduler::schedule()，
 * 如果有新的调度任务，那应该立即退出idle状态，并执行对应的任务；二是关注当前注册的所有IO事件有没有触发，如果有触发，那么应该执行
 * IO事件对应的回调函数
 */
void IOManager::idle() 
{
    LOG_DEBUG("idle()");

    // 超过256的就绪事件会在下一轮的epoll_wait继续处理
    const uint64_t MAX_EVENTS = 256;
    epoll_event *events       = new epoll_event[MAX_EVENTS];
    // 自定义删除器，delete[] 数组
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event *ptr) {
        delete[] ptr;
    });

    while (true) {
        // 获取下一个定时器的超时时间，顺便判断调度器是否停止
        uint64_t next_timeout = 0;
        if (stopping(next_timeout)) {
            LOG_DEBUG("idle() stopping");
            break;
        }

        // 阻塞在epoll_wait上，等待事件触发
        int rt = 0;
        do
        {
            // 默认超时时间为5秒，如果下一个定时器的超时时间大于5秒
            // 仍然以5秒计算超时，避免定时器超时时间太长，epoll_wait一直阻塞
            static const int MAX_TIMEOUT = 5000;
            if (next_timeout != ~0ull) {
                next_timeout = std::min((int)next_timeout, MAX_TIMEOUT);
            } else {
                next_timeout = MAX_TIMEOUT;
            }
            rt = epoll_wait(m_epfd, events, MAX_EVENTS, (int)next_timeout);
            if (rt < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }

        } while (true);

        // 收集所有的已经超时定时器，执行回调函数
        std::vector<std::function<void()>> cbs;
        listExpiredCb(cbs);
        if (!cbs.empty()) {
            for (auto &cb : cbs) {
                schedule(cb);
            }
            cbs.clear();
        }

        // 遍历发生的所有事件，根据epoll_event的私有指针找到对应的FdContext，事件处理
        for (int i = 0; i < rt; ++i) {
            epoll_event &event = events[i];

            // 通知协程调度的事件
            if (event.data.fd == m_tickleFds[0]) {
                // m_tickleFds[0]用于通知协程调度
                // 这时只需要吧管道里的内容读完，本轮idle结束Scheduler::run 会重新执行协程调度
                uint8_t dummy[256];
                while (read(m_tickleFds[0], dummy, sizeof(dummy)) > 0) { ; }
                continue;
            }

            // 要进行协程调度的事件
            // 通过 epoll_event.data.ptr 找到对应的FdContext
            FdContext *fd_ctx = (FdContext *)event.data.ptr;
            FdContext::MutexType::Lock lock(fd_ctx->mutex);
            /**
             * EPOLLERR 出错，比如往读端已经关闭的 pipe 写东西
             * EPOLLHUP 挂断，比如客户端断开连接
             * 这两种情况应该同时触发 fd的读和写事件，否则有可能出现注册的事件永远执行不到的情况
            */
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }
            int real_events = NONE;
            if (event.events & EPOLLIN) {
                real_events |= READ;
            }
            if (event.events & EPOLLOUT) {
                real_events |= WRITE;
            }

            if ((fd_ctx->events & real_events) == NONE) {
                continue;
            }

            // 剔除已经发生的事件，将剩下的事件重新加入epoll_wait
            // 如果剩下的事件为0，则表示这个fd已经不需要关注了，直接从epoll中删除
            int left_events = (fd_ctx->events & ~real_events);
            int op          = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events    = EPOLLET | left_events;

            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if (rt2) {
                LOG_ERROR("idle() epoll_ctl error m_epfd = %d, op = %d, fd = %d, rt2 = %d, errno = %d", m_epfd, op, fd_ctx->fd, rt2, errno);
                continue;
            }

            // 处理已经发生的事件
            if (real_events & READ) {
                fd_ctx->triggerEvent(READ);
                --m_pendingEventCount;
            }
            if (real_events & WRITE) {
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
            }
        } // for

        /**
         * ⼀旦处理完所有的事件，idle协程yield，这样可以让调度协程(Scheduler::run)重新检查是否有新的任务要调度
         * triggerEvent 实际上也只是把对应的fiber重新加入调度，要执行的话还要等idle协程退出
        */
        Fiber::ptr cur = Fiber::GetThis();
        auto raw_ptr = cur.get();
        cur.reset();      // 释放当前协程的引用计数
        raw_ptr->yield(); // 协程让出执行权，让调度协程(Scheduler::run)重新检查是否有新的任务要调度
    } // while
}

/**
 * @brief 返回当前的IOManager
*/
IOManager *IOManager::GetThis()
{
    return dynamic_cast<IOManager *>(Scheduler::GetThis());
}

/**
 * @brief 添加事件
 * @details fd描述符发生了event事件时执行cd回调
 * @param[in] fd sockfd
 * @param[in] event 事件类型
 * @param[in] cb 如果为空，则默认把当前协程当作回调 functor
 * @return 添加成功返回0，否则返回-1，重复添加程序直接退出
*/
int IOManager::addEvent(int fd, Event event, std::function<void()> cb)
{
    // 找到对应的fd对应的fdContext，如果不存在，就分配一个
    FdContext *fd_ctx = nullptr;
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() > fd) {
        fd_ctx = m_fdContexts[fd];
        lock.unlock();
    } else {
        lock.unlock();
        RWMutexType::WriteLock lock2(m_mutex);
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }

    // 同一个 fd 不允许重复添加相同的事件
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (fd_ctx->events & event) {
        LOG_FATAL("addEvent() add same event: fd = %d, event = %d", fd, event);
        // return 1;
    }

    // 将新的事件加入epoll_wait，使用epoll_event的私有 data成员的指针存储 FdContext的位置
    // 新建的 FdContext 默认不关注任何事件，所以需要先添加事件
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event ep_event;
    bzero(&ep_event, sizeof(ep_event));
    ep_event.events     = EPOLLET | fd_ctx->events | event;
    ep_event.data.ptr   = fd_ctx;
    int rt = epoll_ctl(m_epfd, op, fd, &ep_event);
    if (rt) {
        LOG_ERROR("addEvent() epoll_ctl error m_epfd = %d, op = %d, fd = %d, rt = %d, errno = %d", m_epfd, op, fd, rt, errno);
        return -1;
    }

    // 待执行的IO事件数量加1
    ++m_pendingEventCount;
    
    // 找到事件对应的event事件对应的 EventContext，对其中的scheduler，cb，fiber进行赋值
    fd_ctx->events                      = (Event)(fd_ctx->events | event); 
    FdContext::EventContext &event_ctx  = fd_ctx->getEventContext(event);
    if (!(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb)) {
        LOG_FATAL("addEvent() event exist: fd = %d, event = %d", fd, event);
    }

    // 赋值scheduler和回调函数，如果回调函数为空，则把当前协程当成回调函数执行体
    event_ctx.scheduler = Scheduler::GetThis();
    if (cb) {
        event_ctx.cb.swap(cb);
    } else {
        event_ctx.fiber = Fiber::GetThis();
        if (event_ctx.fiber->getState() != Fiber::RUNNING) {
            LOG_FATAL("addEvent() cb is null, but coroutine not running: fd = %d, event = %d, this = %p", fd, event, this);
        }
    }
    return 0;
}

/**
 * @brief 删除事件
 * @param[in] fd sockfd
 * @param[in] event 事件类型
 * @attention 不会触发事件
 * @return 是否删除成功
*/
bool IOManager::delEvent(int fd, Event event)
{
    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex);
    // 找不到直接返回
    if ((int)m_fdContexts.size() <= fd) {
        return false;
    }

    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    // 如果事件不存在，直接返回
    if (!(fd_ctx->events & event)) {
        return false;
    }

    // 清除指定事件，表示已经不再关心该事件，如果清除后结果为0，则从epoll_wait中删除该文件描述符
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event ep_event;
    bzero(&ep_event, sizeof(ep_event));
    ep_event.events  = EPOLLET | new_events;
    ep_event.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &ep_event);
    if (rt) {
        LOG_ERROR("delEvent() epoll_ctl error m_epfd = %d, op = %d, fd = %d, rt = %d, errno = %d", m_epfd, op, fd, rt, errno);
        return false;
    }

    // 执行事件数减1
    --m_pendingEventCount;
    // 重置该 fd 对应的event上下文
    fd_ctx->events              = new_events;
    FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);
    fd_ctx->resetEventContext(event_ctx);
    return true;
}

/**
 * @brief 取消事件
 * @param[in] fd sockfd
 * @param[in] event 事件类型
 * @attention 如果该事件被注册过回调，则触发一次回调
 * @return 是否取消成功
*/
bool IOManager::cancelEvent(int fd, Event event)
{
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) {
        return false;
    }

    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (!(fd_ctx->events & event)) {
        return false;
    }

    // 删除事件
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event ep_event;
    bzero(&ep_event, sizeof(ep_event));
    ep_event.events  = EPOLLET | new_events;
    ep_event.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &ep_event);
    if (rt) {
        LOG_ERROR("cancelEvent() epoll_ctl error m_epfd = %d, op = %d, fd = %d, rt = %d, errno = %d", m_epfd, op, fd, rt, errno);   
        return false;
    }

    // 删除之前触发一次事件
    fd_ctx->triggerEvent(event);
    // 活跃事件数减1
    --m_pendingEventCount;
    return true;
}

/**
 * @brief 取消所有事件
 * @details 所有事件的回调在cancel之前都要执行一次
 * @param[in] fd sockfd
 * @return 是否取消成功
*/
bool IOManager::cancelAll(int fd)
{
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);

    int op = EPOLL_CTL_DEL;
    epoll_event ep_event;
    bzero(&ep_event, sizeof(ep_event));
    ep_event.events  = 0;
    ep_event.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &ep_event);
    if (rt) {
        LOG_ERROR("cancelAll() epoll_ctl error m_epfd = %d, op = %d, fd = %d, rt = %d, errno = %d", m_epfd, op, fd, rt, errno);
        return false;
    }

    // 触发全部已经注册的事件
    if (fd_ctx->events & READ) {
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCount;
    }
    if (fd_ctx->events & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }

    return true;
}

IOManager::~IOManager()
{   // 先等待所有的任务调度完成
    stop();
    // 再关闭 epollfd 和 pipfd
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        if (m_fdContexts[i]) {
            delete m_fdContexts[i];
        }
    }
}

bool IOManager::stopping() 
{
    return m_pendingEventCount == 0 && Scheduler::stopping();
}

/**
 * @brief 判断是否可以停止，同时获取最近的一个定时器的超时时间
 * @param[out] timeout 最近一个定时器的超时时间，用于idle协程的epoll_wait
 * @return 返回是否可以停止
*/
bool IOManager::stopping(uint64_t &timeout)
{
    // 对于IOManager而言，必须等所有待调度的IO事件都执行完了才可以退出
    // 增加定时器功能后，还应该保证没有剩余的定时器待触发
    timeout = getNextTimer();
    return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
}

/**
 * @brief 重置socket句柄上下文的容器大小
 * @param[in] size 容量大小
*/
void IOManager::contextResize(size_t size)
{
    m_fdContexts.resize(size);
    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        if (!m_fdContexts[i]) {
            m_fdContexts[i] = new FdContext;
            m_fdContexts[i]->fd = i;
        }
    }
}

/**
 * @brief 获取事件的上下文类
 * @param[in] event 事件类型
 * @return 返回对应事件的上下文
*/
IOManager::FdContext::EventContext &IOManager::FdContext::getEventContext(Event event)
{
    switch (event)
    {
    case IOManager::READ:
        return read;
    case IOManager::WRITE:
        return write;
    default:
        break;
    }
    throw std::invalid_argument("getContext invalid event");
}

/**
 * @brief 重置事件上下文
 * @param[in, out] ctx 待重置的事件上下文对象
*/
void IOManager::FdContext::resetEventContext(EventContext &ctx)
{
    ctx.scheduler = nullptr;
    ctx.fiber.reset();
    ctx.cb = nullptr;
}

/**
 * @brief 触发事件
 * @details 根据事件类型调用对应上下文结构中的调度器去调度 回调协程或者回调函数
 * @param[in] event 事件类型
*/
void IOManager::FdContext::triggerEvent(Event event)
{
    // 待触发的事件必须已经被注册
    if (!(events & event)) {
        LOG_FATAL("triggerEvent() not registed event: events = %d, event = %d", events, event);
    }

    // 清除该事件，表示不再关注该事件了
    // 注册的IO事件是一次性的，如果想持续关注某个sock fd 上的读写事件，那么每次触发事件之后都要重新添加
    events = (Event)(events & ~event);

    // 调度对应的协程
    EventContext &ctx = getEventContext(event);
    if (ctx.cb) {
        // fixbug
        ctx.scheduler->schedule(ctx.cb);
    } else if (ctx.fiber) {
        // fixbug
        ctx.scheduler->schedule(ctx.fiber);
    }
    resetEventContext(ctx);
    return;
}

} // namespace sylar
