/**
* @file  thread.h
* @author chenxueyou
* @version 0.1
* @brief   :A asymmetric coroutine library for C++
* History
*      1. Date: 2014-12-12 
*          Author: chenxueyou
*          Modification: this file was created 
*/

#ifndef MY_UTHREAD_H
#define MY_UTHREAD_H

#ifdef __APPLE__
#define _XOPEN_SOURCE
#endif 

#include <ucontext.h>
#include <vector>

#define DEFAULT_STACK_SZIE (1024*128)
#define MAX_UTHREAD_SIZE   1024

/**
 * 协程状态 
 * FREE: 空闲状态
 * RUNNABLE: 可运行状态
 * RUNNING: 正在运行状态
 * SUSPEND: 挂起状态
 * */ 
enum ThreadState{FREE,RUNNABLE,RUNNING,SUSPEND};

struct schedule_t;

typedef void (*Fun)(void *arg);

// 协程结构体
typedef struct uthread_t
{
    // 协程上下文
    ucontext_t ctx;
    // 协程执行的用户级函数
    Fun func;
    // 协程执行的用户级函数的参数
    void *arg;
    // 协程的当前状态
    enum ThreadState state;
    // 协程栈
    char stack[DEFAULT_STACK_SZIE];
}uthread_t;

// 调度器结构体
typedef struct schedule_t
{
    // 主函数的上下文
    ucontext_t main;
    // 当前正在运行的协程编号，如果没有正在执行的协程，为-1
    int running_thread;
    // 调度器所拥有的协程
    uthread_t *threads;
    // 曾经使用到的最大的index + 1
    int max_index; 

    schedule_t():running_thread(-1), max_index(0) {
        threads = new uthread_t[MAX_UTHREAD_SIZE];
        for (int i = 0; i < MAX_UTHREAD_SIZE; i++) {
            threads[i].state = FREE;
        }
    }
    
    ~schedule_t() {
        delete [] threads;
    }
}schedule_t;

/*help the thread running in the schedule*/
static void uthread_body(schedule_t *ps);

/*Create a user's thread
*    @param[in]:
*        schedule_t &schedule 
*        Fun func: user's function
*        void *arg: the arg of user's function
*    @param[out]:
*    @return:
*        return the index of the created thread in schedule
*/
int  uthread_create(schedule_t &schedule,Fun func,void *arg);

/* Hang the currently running thread, switch to main thread */
void uthread_yield(schedule_t &schedule);

/* resume the thread which index equal id*/
void uthread_resume(schedule_t &schedule,int id); 

/*test whether all the threads in schedule run over
* @param[in]:
*    const schedule_t & schedule 
* @param[out]:
* @return:
*    return 1 if all threads run over,otherwise return 0
*/
int  schedule_finished(const schedule_t &schedule);

#endif