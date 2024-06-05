## 协程框架执行流程

### 1. 程序主线程创建一个 IOManager

```c++
/**
  * @brief 构造函数
  * @param[in] threads 线程数量
  * @param[in] use_caller 是否将调用线程包含进去
  * @param[in] name 调度器的名称
  */
IOManager(size_t threads = 1, bool use_caller = true, const std::string &name = "IOManager");
```

### 2. 添加监听socket描述符和事件回调

```C++
/**
  * @brief 添加事件
  * @details fd描述符发生了event事件时执行cd回调
  * @param[in] fd sockfd
  * @param[in] event 事件类型
  * @param[in] cb 如果为空，则默认把当前协程当作回调 functor
  * @return 添加成功返回0，否则返回1
  */
int addEvent(int fd, Event event, std::function<void()> cb = nullptr);
	
// 对监听描述符绑定回调为 accept 逻辑如下：
void test_accept()
{
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    socklen_t len = sizeof(addr);
    int fd = accept(sock_listen_fd, (struct sockaddr*)&addr, &len);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    // 将新连接的可写事件添加，绑定回调
    // addEvent内部将Fd添加到epoll中，并绑好对应的scheduler和协程上下文
    sylar::IOManager::GetThis()->addEvent(fd, sylar::IOManager::READ, [fd]() {
        char buffer[1024] = {0};
        bzero(buffer, sizeof(buffer));
        while (true)
        {
            // recv和send都是被hook的调用
            int ret = recv(fd, buffer, sizeof(buffer), 0);
            if (ret > 0) {
                ret = send(fd, buffer, ret, 0);
            }
            if (ret <= 0) {
                if (errno == EAGAIN) continue;
                close(fd);
                break;
            }
        }
    });
    // 这里是服务器的逻辑，非阻塞接受一个链接之后再进行 schedule 调用
    // 其实就是再次 schedule 了 test_accept，
    // schedule 其实就是把任务添加到调度器内部的任务队列上
    sylar::IOManager::GetThis()->schedule(watch_io_read);
}


```

## 3. 调度器的工作从 构造函数 开始

构造函数中初始化调度器线程池，为每个线程都指定线程执行函数 scheduler::run()

```C++
void Scheduler::run()
{
    LOG_DEBUG("Scheduler::run() begin");
    setThis();

    // 不等于caller时要创建调度协程
    if (sylar::GetThreadId() != m_rootThread) {
        t_scheduler_fiber = sylar::Fiber::GetThis().get();
    }

    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
    Fiber::ptr cb_fiber;

    ScheduleTask task;
    while (true) {
        task.reset();
        
        // 是否tickle 其他线程进行任务调度
        bool tickle_me = false; 
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
                    continue;
                }
                // 找到一个未指定线程，或是指定了当前线程的任务
                if (!(it->fiber || it->cb)) {
                    LOG_FATAL("it->fiber == nullptr && it->cb == nullptr");
                }

                // [BUG FIX]
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
```

没有任务时 resume idle协程，idle协程在scheduler中定义为虚函数，实际不搞什么事情只是来回切换，在子类IOManager中override为新的idle，在IOManager中传入Scheduler::run()的this指针为IOManager，绑定在idle_fiber上的为其自己的idle，这里是多态特性的使用

```C++
/**
 * 调度器无调度任务时会阻塞在idle协程上，对IO调度器而言，idle状态应该关注两件事，
 * 一是有没有新的调度任务，对应Schduler::schedule()，
 * 如果有新的调度任务，那应该立即退出idle状态，并执行对应的任务；
 * 
 * 二是关注当前注册的所有IO事件有没有触发，如果有触发，那么应该执行
 * IO事件对应的回调函数
 */
void IOManager::idle() 
{
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
            break;
        }

        // 阻塞在epoll_wait上，等待事件触发
        int rt = 0;
        do
        {
            // 默认超时时间为5秒，如果下一个定时器的超时时间大于5秒
            // 仍然以5秒计算超时，避免定时器超时时间太长，epoll_wait一直阻塞
            static const int MAX_TIMEOUT = 5000;
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
             * 这两种情况应该同时触发 fd的读和写事件，
             * 否则有可能出现注册的事件永远执行不到的情况
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
         * ⼀旦处理完所有的事件，idle协程yield，
         * 这样可以让调度协程(Scheduler::run)重新检查是否有新的任务要调度
         * 
         * triggerEvent 实际上也只是把对应的fiber重新加入调度，
         * 要执行的话还要等idle协程退出
        */
        Fiber::ptr cur = Fiber::GetThis();
        auto raw_ptr = cur.get();
        // 释放当前协程的引用计数
        cur.reset();      

        // 协程让出执行权，
        // 让调度协程(Scheduler::run)重新检查是否有新的任务要调度
        raw_ptr->yield(); 
    } // while
}
```

其实就是run和idle来回相互切换，对于tickle其实就是往调度器的pipfd里面写一下，让其他空闲线程从idle的epoll_wait中返回

```C++
/**
 * @brief 通知调度器有任务要调度
 * @details 写pipe 让idle协程从epoll_wait中返回,
 *          待idle协程yield后,Scheduler::run() 就可以调度其他任务
 *          如果当前没有空闲调度线程，那就没有必要发通知
*/
void IOManager::tickle()
{
    if (!hasIdleThreads()) {
        return;
    }
    int rt = write(m_tickleFds[1], "T", 1);
    if (rt != 1) {
        LOG_FATAL("tickle() write error");
    }
}
```

对于triggerEvent

```C++
/**
 * @brief 触发事件
 * @details 根据事件类型调用对应上下文结构中的调度器去调度 回调协程或者回调函数
 * @param[in] event 事件类型
*/
void IOManager::FdContext::triggerEvent(Event event)
{
    // 待触发的事件必须已经被注册
    if (!(events & event)) {
        LOG_FATAL("");
    }

    // 清除该事件，表示不再关注该事件了
    // 注册的IO事件是一次性的，如果想持续关注某个sock fd 上的读写事件，
    // 那么每次触发事件之后都要重新添加
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
```

对于schedule也只是把任务提交到任务队列

```C++
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
    // 任务队列非空的话就不tickle了
    return need_tickle;
}
```

## 4. 使用同步接口进行异步编程

将用户提交过来的同步IO转化为异步IO，echo服务器中的

```C++
int ret = recv(fd, buffer, sizeof(buffer), 0);
ret = send(fd, buffer, ret, 0);
```

两个系统调用其实已经被hook过了，

hook的实现如下

```C++
ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    return do_io(sockfd, recv_f, "recv", sylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return do_io(fd, write_f, "write", sylar::IOManager::WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t send(int s, const void *msg, size_t len, int flags) {
    return do_io(s, send_f, "send", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags);
}

template<typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char* hook_fun_name,
        uint32_t event, int timeout_so, Args&&... args) {
    if(!sylar::t_hook_enable) {
        return fun(fd, std::forward<Args>(args)...);
    }

    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
    if(!ctx) {
        return fun(fd, std::forward<Args>(args)...);
    }

    if(ctx->isClosed()) {
        errno = EBADF;
        return -1;
    }

    if(!ctx->isSocket() || ctx->getUserNonblock()) {
        return fun(fd, std::forward<Args>(args)...);
    }

    uint64_t to = ctx->getTimeout(timeout_so);
    std::shared_ptr<timer_info> tinfo(new timer_info);

retry:
    ssize_t n = fun(fd, std::forward<Args>(args)...);
    while(n == -1 && errno == EINTR) {
        n = fun(fd, std::forward<Args>(args)...);
    }
    // 非阻塞
    if(n == -1 && errno == EAGAIN) {
        sylar::IOManager* iom = sylar::IOManager::GetThis();
        sylar::Timer::ptr timer;
        std::weak_ptr<timer_info> winfo(tinfo);

        if(to != (uint64_t)-1) {
            timer = iom->addConditionTimer(to, [winfo, fd, iom, event]() {
                auto t = winfo.lock();
                if(!t || t->cancelled) {
                    return;
                }
                t->cancelled = ETIMEDOUT;
                iom->cancelEvent(fd, (sylar::IOManager::Event)(event));
            }, winfo);
        }

        int rt = iom->addEvent(fd, (sylar::IOManager::Event)(event));
        // 成功添加事件
        if(!rt) {
            if(timer) {
                timer->cancel();
            }
            return -1;
        } else {
            // yield当前协程，等待再次执行
            // 如果成功执行了IO操作，则系统调用会返回0，下一次走不到后面的goto retry，直接返回n
            sylar::Fiber::GetThis()->yield();
            if(timer) {
                timer->cancel();
            }
            if(tinfo->cancelled) {
                errno = tinfo->cancelled;
                return -1;
            }
            goto retry;
        }
    }
    
    return n;
}
```

sleep也是被hook的，hook为定时事件，总不能真让线程睡吧

```C++
unsigned int sleep(unsigned int seconds) {
    if (!sylar::t_hook_enable) {
        return sleep_f(seconds);
    }

    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    sylar::IOManager *iom = sylar::IOManager::GetThis();

    // 添加定时事件，回调函数绑为当前协程
    // 其实就是把当前协程添加进调度器，然后yield
    // 到超时时间之后再在超时回调中执行
    // 对应的在 Scheduler::run() 中再次 resume
    iom->addTimer(seconds*1000, std::bind(
        (void(sylar::Scheduler::*)(sylar::Fiber::ptr,intthread))
        &sylar::IOManager::schedule,iom, 
        fiber, -1));
    sylar::Fiber::GetThis()->yield();
    return 0;                                
}
```





