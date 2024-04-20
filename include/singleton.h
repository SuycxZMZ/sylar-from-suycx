#ifndef SINGLETON_H
#define SINGLETON_H

/**
 * @file singleton.h
 * @author sylar, suycx
 * @brief 单例模式封装
 * @version 0.1
 * @date 2024-04-19
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include <memory>

namespace sylar
{

namespace 
{

template <class T, class X, int N>
T& GetInstanceX() {
    static T v;
    return v;
}

template <class T, class X, int N>
std::shared_ptr<T> GetInstancePtr() {
    static std::shared_ptr<T> v(new T);
    return v;   
}

}

/**
 * @brief 单例模式封装
 * @details T 类型
 *          X 为了创造多个实例对应的Tag
 *          N 同一个 Tag 创造多个实例索引
 * @return T* 
 */
template <class T, class X = void, int N = 0>
class Singleton
{
public:
    /**
     * @brief 返回单例裸指针
     * 
     * @return T* 
     */
    static T* GetInstance() {
        static T v;
        return &v;
    }
};

template <class T, class X, int N = 0>
class SingletonPtr 
{
public:
    static std::shared_ptr<T> GetInstance() {
        static std::shared_ptr<T> v(new T);
        return v;
    }
};

}

#endif