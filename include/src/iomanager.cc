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

} // namespace sylar
