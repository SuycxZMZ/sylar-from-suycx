#include <list>
#include <iostream>

#include "fiber.h"

/**
 * @brief 简单的协程调度器, 支持添加调度任务以及运行调度任务
*/
class Scheduler
{
public:
    void schedule(sylar::Fiber::ptr task) 
    {
        m_tasks.push_back(task);
    }
    void run()
    {
        sylar::Fiber::ptr task;
        auto it = m_tasks.begin();
        while (it != m_tasks.end())
        {
            task = *it;
            m_tasks.erase(it++);
            task->resume();
        }
    }
private:
    std::list<sylar::Fiber::ptr> m_tasks;
};

void test_fiber(int i)
{
    std::cout << "hello world" << i << std::endl;
}

int main()
{
    // 初始化当前线程的主协程
    sylar::Fiber::GetThis();

    // 创建调度器
    Scheduler scheduler;

    // 添加调度任务
    for (int i = 0; i < 10; i++) {
        sylar::Fiber::ptr fiber(new sylar::Fiber(std::bind(test_fiber, i)));
        scheduler.schedule(fiber);
    }

    scheduler.run();

    return 0;
}