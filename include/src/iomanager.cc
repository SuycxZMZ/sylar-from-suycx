#include <sys/epoll.h>
#include <unistd.h>
#include <strings.h>
#include <fcntl.h>
#include <errno.h>

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

    const uint64_t MAX_EVENTS = 256;
    epoll_event *events       = new epoll_event[MAX_EVENTS];
    // 自定义删除器，delete[] 数组
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event *ptr) {
        delete[] ptr;
    });

    while (true) {
        if (stopping()) {
            LOG_DEBUG("idle() stopping");
            break;
        }

        // 阻塞在epoll_wait上，等待事件触发
        static const int MAX_TIMEOUT = 5000;
        int rt = epoll_wait(m_epfd, events, MAX_EVENTS, MAX_TIMEOUT);
        if (rt < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERROR("idle() epoll_wait error m_epfd = %d, rt = %d, errno = %d", m_epfd, rt, errno);
            break;
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
                LOG_ERROR("idle() epoll_ctl error m_epfd = %d, op = %d, fd = %d, rt2 = %d, errno = %d");
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
    
}

} // namespace sylar
