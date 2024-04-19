#ifndef TIMER_H
#define TIMER_H

#include <time.h>
#include <memory>
#include <set>
#include <vector>
#include <functional>

#include "mutex.h"
#include "util.h"

namespace sylar
{
class TimerManager;

class Timer : public std::enable_shared_from_this<Timer>
{
friend class TimerManager;
public:
    using ptr = std::shared_ptr<Timer>;

    bool cancel();
    bool refresh();

    /**
     * @brief 重置定时器时间
     * @param[in] ms 定时器执行间隔
     * @param[in] from_now 是否从现在开始计时
    */
    bool reset(uint64_t ms, bool from_now);
private:
    /**
     * @brief 构造函数
     * @param[in] ms 定时器执行间隔
     * @param[in] cb 到期回调
     * @param[in] recurring 是否循环定时器
     * @param[in] manager 定时器管理器
    */
    Timer(uint64_t ms, std::function<void()> cb,
          bool recurring, TimerManager *manager);
    
    /**
     * @brief 构造函数
     * @param[in] next 定时器下一次执行时间,时间戳(ms)
    */
    Timer(uint64_t next);
private:
    // 是否循环定时器
    bool m_recurring = false;
    // 执行周期
    uint64_t m_ms = 0;
    // 精确执行时间
    uint64_t m_next = 0;
    // 到期回调
    std::function<void()> m_cb;
    // 定时器管理器
    TimerManager *m_manager = nullptr;
private:
    struct Comparator {
        /**
         * @brief 比较两个定时器智能指针的大小
         * @param[in] lhs 定时器1智能指针
         * @param[in] rhs 定时器2智能指针
        */
        bool operator() (const Timer::ptr &lhs, const Timer::ptr &rhs) const;
    };
};

class TimerManager
{
friend class Timer;
public:
    using RWMutexType = RWMutex;

    /**
     * @brief 构造函数
    */
    TimerManager();

    /**
     * @brief 析构函数
    */
    virtual ~TimerManager();

    /**
     * @brief 添加定时器
     * @param[in] ms 定时器执行间隔时间
     * @param[in] cb 到期回调
     * @param[in] recurring 是否循环定时器
    */
    Timer::ptr addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);

    /**
     * @brief 添加条件定时器
     * @param[in] ms 定时器执行间隔时间
     * @param[in] cb 到期回调
     * @param[in] weak_cond 条件
     * @param[in] recurring 是否循环定时器
    */
    Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb,
                                 std::weak_ptr<void> weak_cond,
                                 bool recurring = false);

    /**
     * @brief 找到最近一个定时器执行的时间间隔(ms)
    */
    uint64_t getNextTimer();

    /**
     * @brief 获取需要执行的定时器回调函数列表
     * @param[in] cbs 回调函数数组
    */
    void listExpiredCb(std::vector<std::function<void()> > &cbs);

    /**
     * @brief 是否有定时器
    */
    bool hasTimer();
protected:
    /**
     * @brief 当有新的定时器插入到首部，执行该函数
    */
    virtual void onTimerInsertedAtFront() = 0; 

    /**
     * @brief 将定时器添加到管理器当中
    */
    void addTimer(Timer::ptr val, RWMutexType::WriteLock &lock);
private:

    /**
     * @brief 检测服务器时间是否被调后了
    */
    bool detectClockRollover(uint64_t now_ms);
private:
    RWMutexType m_mutex;
    // 定时器集合
    std::set<Timer::ptr, Timer::Comparator> m_timers;
    // 是否触发 onTimerInsertedAtFront
    bool m_tickled = false;
    // 上次执行时间
    uint64_t m_previouseTime = 0;
};

} // namespace sylar




#endif // TIMER_H