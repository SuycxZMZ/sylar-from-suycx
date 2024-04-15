#ifndef NONCOPYABLE_H
#define NONCOPYABLE_H

/*
派生对象可以正常构造和析构
不能拷贝和赋值
*/
class noncopyable
{
public:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator=(const noncopyable&) = delete;
protected:
    noncopyable() = default;
    ~noncopyable() = default;
};

#endif