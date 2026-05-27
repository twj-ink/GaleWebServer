/* threadpool.h
 * 
 * author: GaleInk
 * date: 2026/05/12
 */

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <cstdio>
#include <unistd.h>
#include "locker.h"

template <typename T>
class threadpool
{
public:
    threadpool(int thread_num = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T* request); // 主线程使用它把任务加进队列

private:
    // 任务队列
    std::list<T*> m_workqueue;

    // 线程相关
    int m_thread_num; // 线程个数
    int m_max_requests; // 队列最大长度
    pthread_t* m_threads; // 线程ID数组，构造时new出来
    bool m_stop; // 关闭标志

    // 同步工具
    locker m_mutex; // 这个locker是对于工作队列的互斥锁，保证取任务和放任务的原子性
    cond m_cond; // 条件变量，是通知机制，当有新任务的时候就通知线程来处理

    // 线程函数
    static void* worker(void* arg); // pthread_create要求static，arg传递this
    void run(); // 真正的干活循环，worker内部调用
};

template <typename T>
threadpool<T>::threadpool(int thread_num, int max_requests)
{
    // 构造函数
    // 1. 合法
    if (thread_num <= 0) thread_num = 1;
    if (max_requests <= 0) max_requests = 1;
    // 2. 赋值
    m_thread_num = thread_num;
    m_max_requests = max_requests;
    m_threads = new pthread_t[thread_num];
    m_stop = false;
    // 3. 创建thread_num个线程
    for (int i = 0; i < thread_num; i++) {
        pthread_create(&m_threads[i], nullptr, worker, this);
        pthread_detach(m_threads[i]);
    }
}

template <typename T>
threadpool<T>::~threadpool()
{
    // 析构函数
    m_stop = true;
    // 唤醒所有线程，在run的while检查中会因为stop而结束
    m_cond.broadcast();
    delete[] m_threads;
}

template <typename T>
void* threadpool<T>::worker(void* arg)
{
    // 线程的工作函数
    // 首先arg是传入的this，进行强制类型转换，也就是把线程池对象传进来
    threadpool* pool = (threadpool*)arg;
    // 然后调用线程池的run函数来运行线程
    pool->run();
    return nullptr;
}

template <typename T>
void threadpool<T>::run()
{
    // 现在线程要启动了，一个循环
    while (true)
    {
        // 1. 获取锁
        m_mutex.lock();
        // 2. 如果队列为空 且 没有停止 就等
        while (m_workqueue.empty() && !m_stop) {
            // 这个wait会自动放弃锁，然后线程等待，直到在append函数中signal_one了才唤醒
            // 重新获取锁，继续处理
            m_cond.wait(m_mutex.get());
        }

        if (m_stop) {
            m_mutex.unlock();
            break;
        }

        // 3. 从队列中取出一个任务
        T* request = m_workqueue.front();
        m_workqueue.pop_front();

        // 4. 保证**取出任务**的原子性
        m_mutex.unlock();

        // 执行HTTP请求
        request->process();
    }
}

template <typename T>
bool threadpool<T>::append(T* request)
{
    // 1. 获取锁
    m_mutex.lock();
    // 2. 如果队列慢了就解锁，返回false
    if ((int)m_workqueue.size() >= m_max_requests) {
        m_mutex.unlock();
        return false;
    }
    // 3. 加入
    m_workqueue.push_back(request);
    // 4. 唤醒一个在run中wait的睡眠线程
    m_cond.signal_one();
    // 5. 解锁，返回
    m_mutex.unlock();
    return true;
}

#endif // THREADPOOL_H

// class testtask
// {
// private:
//     int m_id;
// public:
//     testtask(int id) : m_id(id) {};
//     void process() {
//         printf("thread %lu is processing test_task %d\n", pthread_self(), m_id);
//         usleep(100000);
//     }
// };