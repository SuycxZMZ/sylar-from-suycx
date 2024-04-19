#include "timer.h"


namespace sylar
{
bool Timer::Comparator::operator() (const Timer::ptr &lhs,
                                    const Timer::ptr &rhs) const
{
    if (!lhs && !rhs) {
        return false;
    }
    if (!lhs) {
        return true;
    }
    if (!rhs) {
        return false;
    }
    if (lhs->m_next < rhs->m_next) {
        return true;
    }
    if (rhs->m_next < lhs->m_next) {
        return false;
    }
    return lhs.get() < rhs.get();
}

/**
 * @brief 构造函数
 * @param[in] ms 定时器执行间隔
 * @param[in] cb 到期回调
 * @param[in] recurring 是否循环定时器
 * @param[in] manager 定时器管理器
*/
Timer::Timer(uint64_t ms, std::function<void()> cb,
        bool recurring, TimerManager *manager) : 
    m_recurring(recurring),
    m_ms(ms),
    m_cb(cb),
    m_manager(manager)
{
    m_next = sylar::GetElapsedMS() + m_ms;
}

/**
 * @brief 构造函数
 * @param[in] next 定时器下一次执行时间,时间戳(ms)
*/
Timer::Timer(uint64_t next) : m_next(next)
{
}

bool Timer::cancel()
{
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if (m_cb) {
        m_cb = nullptr;
        auto it = m_manager->m_timers.find(shared_from_this());
        m_manager->m_timers.erase(it);
        return true;
    }
    return false;
}

bool Timer::refresh() 
{
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if (!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    if (it == m_manager->m_timers.end()) {
        return false;
    }
    m_manager->m_timers.erase(it);
    m_next = sylar::GetElapsedMS() + m_ms;
    m_manager->m_timers.insert(shared_from_this());
    return true;
}

/**
 * @brief 重置定时器时间
 * @param[in] ms 定时器执行间隔
 * @param[in] from_now 是否从现在开始计时
*/
bool Timer::reset(uint64_t ms, bool from_now)
{
    if (ms == m_ms && !from_now) {
        return true;
    }
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if (!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    if (it == m_manager->m_timers.end()) {
        return false;
    }
    m_manager->m_timers.erase(it);
    uint64_t start = 0;
    if (from_now) {
        start = sylar::GetElapsedMS();
    } else {
        start = m_next - m_ms;
    }

    m_ms = ms;
    m_next = start + m_ms;
    m_manager->addTimer(shared_from_this(), lock);
    return true;
}

TimerManager::TimerManager()
{
    m_previouseTime = sylar::GetElapsedMS();
}

TimerManager::~TimerManager()
{
}

/**
 * @brief 添加定时器
 * @param[in] ms 定时器执行间隔时间
 * @param[in] cb 到期回调
 * @param[in] recurring 是否循环定时器
*/
Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring)
{
    Timer::ptr timer(new Timer(ms, cb, recurring, this));
    RWMutexType::WriteLock lock(m_mutex);
    addTimer(timer, lock);
    return timer;
}

static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb)
{
    std::shared_ptr<void> tmp = weak_cond.lock();
    if (tmp) {
        cb();
    }
}

/**
 * @brief 添加条件定时器
 * @param[in] ms 定时器执行间隔时间
 * @param[in] cb 到期回调
 * @param[in] weak_cond 条件
 * @param[in] recurring 是否循环定时器
*/
Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb,
                                std::weak_ptr<void> weak_cond,
                                bool recurring)
{
    return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

/**
 * @brief 找到最近一个定时器执行的时间间隔(ms)
*/
uint64_t TimerManager::getNextTimer()
{
    RWMutexType::ReadLock lock(m_mutex);
    m_tickled = false;
    if (m_timers.empty()) {
        return ~0ULL;
    }

    const sylar::Timer::ptr & next = *m_timers.begin();
    uint64_t now_ms = sylar::GetElapsedMS();
    if (now_ms >= next->m_next) {
        return 0;
    } else {
        return next->m_next - now_ms;
    }
}

/**
 * @brief 获取需要执行的定时器回调函数列表
 * @param[in] cbs 回调函数数组
*/
void TimerManager::listExpiredCb(std::vector<std::function<void()> > &cbs)
{
    uint64_t now_ms = sylar::GetElapsedMS();
    std::vector<Timer::ptr> expired;
    {
        RWMutexType::ReadLock lock(m_mutex);
        if (m_timers.empty()) {
            return;
        }
    }
    RWMutexType::WriteLock lock(m_mutex);
    if (m_timers.empty()) {
        return;
    }

    bool rollover = false;
    if (!detectClockRollover(now_ms)) {
        rollover = true;
    }
    if (!rollover && ((*m_timers.begin())->m_next > now_ms)) {
        return;
    }

    Timer::ptr now_timer(new Timer(now_ms));
    auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);
    while (it != m_timers.end() && (*it)->m_next == now_ms) {
        ++it;
    }
    expired.insert(expired.begin(), m_timers.begin(), it);
    m_timers.erase(m_timers.begin(), it);
    cbs.reserve(expired.size());
    for (auto &timer : expired) {
        cbs.push_back(timer->m_cb);
        if (timer->m_recurring) {
            timer->m_next = now_ms + timer->m_ms;
            m_timers.insert(timer);
        } else {
            timer->m_cb = nullptr;
        }
    }
}

/**
 * @brief 将定时器添加到管理器当中
*/
void TimerManager::addTimer(Timer::ptr val, RWMutexType::WriteLock &lock)
{
    auto it = m_timers.insert(val).first;
    bool at_front = (it == m_timers.begin()) && !m_tickled;
    if (at_front) {
        m_tickled = true;
    }
    lock.unlock();

    if (at_front) {
        onTimerInsertedAtFront();
    }
}

/**
 * @brief 检测服务器时间是否被调后了
*/
bool TimerManager::detectClockRollover(uint64_t now_ms)
{
    bool rollover = false;
    if (now_ms < m_previouseTime && 
            now_ms < (m_previouseTime - 60 * 60 * 1000)) {
        rollover = true;
    }
    m_previouseTime = now_ms;
    return rollover;
}

/**
 * @brief 是否有定时器
*/
bool TimerManager::hasTimer()
{
    RWMutexType::ReadLock lock(m_mutex);
    return !m_timers.empty();
}

} // namespace sylar
