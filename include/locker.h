/* locker.h
 * RAII 互斥锁、信号量、条件变量封装
 *
 * author: GaleInk
 * date: 2026/05/12
 */

#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <semaphore.h>

// 互斥锁：保护共享资源，同时只有一个线程能访问
class locker
{
public:
    locker()
    {
        pthread_mutex_init(&m_mutex, NULL);
    }

    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    pthread_mutex_t* get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};


// RAII 自动锁：构造时加锁，析构时解锁
// 用法：在函数开头声明 scope_lock lk(&my_locker);
//       函数结束时自动解锁，即使中间 return 或抛异常也保证解锁
class scope_lock
{
public:
    scope_lock(locker* lk) : m_locker(lk)
    {
        m_locker->lock();
    }

    ~scope_lock()
    {
        m_locker->unlock();
    }

private:
    locker* m_locker;
};


// 信号量：控制可用资源数量（比如线程池中有几个空闲线程）
class sem
{
public:
    sem()
    {
        sem_init(&m_sem, 0, 0);
    }

    sem(int value)
    {
        sem_init(&m_sem, 0, value);
    }

    ~sem()
    {
        sem_destroy(&m_sem);
    }

    // P 操作：资源数 -1，如果没有可用资源就阻塞等待
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }

    // V 操作：资源数 +1，唤醒一个等待的线程
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};


// 条件变量：线程间通信，"有活儿了通知我" 或 "活儿干完了"
class cond
{
public:
    cond()
    {
        pthread_cond_init(&m_cond, NULL);
    }

    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }

    // 等待条件满足，同时临时释放锁
    bool wait(pthread_mutex_t* mutex)
    {
        return pthread_cond_wait(&m_cond, mutex) == 0;
    }

    // 有超时的等待，超时返回 false
    bool wait_timeout(pthread_mutex_t* mutex, struct timespec* ts)
    {
        return pthread_cond_timedwait(&m_cond, mutex, ts) == 0;
    }

    // 唤醒一个等待的线程
    bool signal_one()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }

    // 唤醒所有等待的线程
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;
};

#endif // LOCKER_H
