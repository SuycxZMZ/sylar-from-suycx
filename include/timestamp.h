#ifndef TIMESTAMP_H
#define TIMESTAMP_H
#include <iostream>
#include <string>

class Timestamp
{
public:
    Timestamp();


    explicit Timestamp(int64_t microSecondsSinceEpoch);


    static Timestamp now();


    std::string toString() const;
    //格式, "%4d年%02d月%02d日 星期%d %02d:%02d:%02d.%06d",时分秒.微秒
    std::string toFormattedString(bool showMicroseconds = false) const;
    //返回当前时间戳的微秒
    int64_t microSecondsSinceEpoch() const { return m_microSecondsSinceEpoch; }
    //返回当前时间戳的秒数
    time_t secondsSinceEpoch() const
    { 
        return static_cast<time_t>(m_microSecondsSinceEpoch / kMicroSecondsPerSecond); 
    }    
    // 失效的时间戳，返回一个值为0的Timestamp
    static Timestamp invalid()
    {
        return Timestamp();
    }

    // 1秒=1000*1000微妙
    static const int kMicroSecondsPerSecond = 1000 * 1000;
private:
    int64_t m_microSecondsSinceEpoch;
};

/**
 * 定时器需要比较时间戳，因此需要重载运算符
 */
inline bool operator<(Timestamp lhs, Timestamp rhs)
{
    return lhs.microSecondsSinceEpoch() < rhs.microSecondsSinceEpoch();
}

inline bool operator==(Timestamp lhs, Timestamp rhs)
{
    return lhs.microSecondsSinceEpoch() == rhs.microSecondsSinceEpoch();
}

// 如果是重复定时任务就会对此时间戳进行增加。
inline Timestamp addTime(Timestamp timestamp, double seconds)
{
    // 将延时的秒数转换为微妙
    int64_t delta = static_cast<int64_t>(seconds * Timestamp::kMicroSecondsPerSecond);
    // 返回新增时后的时间戳
    return Timestamp(timestamp.microSecondsSinceEpoch() + delta);
}


#endif