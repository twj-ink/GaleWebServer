/* log.cpp
 * 
 * author: GaleInk
 * date: 2026/05/22
 */

#include "include/log.h"
#include <charconv>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

Log::Log()
{
    m_fp = nullptr;
    m_mode = 0; // default = 同步
    m_running = false;
    m_log_queue = nullptr;
    m_max_queue_size = 0;
    m_queue_count = 0;
    m_queue_front = 0;
    m_queue_rear = 0;
    m_dropped = 0;
}

Log::~Log()
{
    shutdown();
}

Log* Log::get_instance()
{
    static Log instance;
    return &instance;
}

bool Log::init(const char* file_name, int mode, int max_queue_size)
{
    m_mode = mode;
    if (m_mode == 1) { // 异步
        m_max_queue_size = max_queue_size > 0 ? max_queue_size : 1024;
        m_log_queue = new char*[m_max_queue_size];  // 只分配指针数组
        m_running = true;
        pthread_create(&m_async_thread, nullptr, async_write_log, this);
        pthread_detach(m_async_thread);
    }

    /*
    mode a: Open for appending (writing at end of file).  
    The file is created if it does not exist.  
    The stream is positioned at the end of the file.
    */
    m_fp = fopen(file_name, "a");
    if (!m_fp)
    {
        fprintf(stderr, "ERROR: cannot open log file %s\n", file_name);
        return false;
    }
    return true;
}

// 业务线程把日志格式化，放入队列，算是生产者
void Log::write_log(LogLevel level, const char* file, int line, const char* format, ...)
{
    time_t now = time(nullptr);
    struct tm* time_info = localtime(&now);
    const char* level_str[] = { "", "INFO", "WARN", "ERROR"};
    char log_buf[2048];
// RETURN VALUE
//        Upon successful return, these functions return the number of bytes printed (excluding the null  byte  used  to
//        end output to strings).
    // n可以看作log_buf的index
    int n = snprintf(log_buf, sizeof(log_buf), "[%02d:%02d:%02d][%s][%s:%d] ", 
        time_info->tm_hour, time_info->tm_min, time_info->tm_sec, 
        level_str[level], file, line);
    
    // ① va_list: 声明一个"指针"，用来遍历参数
    va_list args;
    // ② va_start: 让 args 指向 format 之后第一个可变参数
    va_start(args, format);
    //             ↑ 第二个参数format是最后一个固定参数的名字
    // ③ vsnprintf: 按 format 把可变参数拼成字符串
    n += vsnprintf(log_buf + n, sizeof(log_buf) - n, format, args);
    va_end(args);
    log_buf[n++] = '\n';
    log_buf[n] = '\0';

    if (m_mode == 0) {
        // 同步
        m_mutex.lock();
        fputs(log_buf, m_fp); // Write a string to STREAM
        // fputs到 C 库的缓冲区（还没到磁盘）
        fflush(m_fp); // 强制把缓冲区内容写到磁盘 
        m_mutex.unlock();
    }
    else {
        // 异步
        m_mutex.lock();
        // 先全部写到m_log_queue
        if (m_queue_count < m_max_queue_size) {
            m_log_queue[m_queue_rear] = strdup(log_buf);
            // 用strdup拷贝到堆上，保证这个函数返回之后，即使log_buf在栈上，日志数组内容是还在的
            m_queue_rear = (m_queue_rear + 1) % m_max_queue_size;
            m_queue_count++;
            m_cond.signal_one(); // 唤醒一个正在wait的后台线程
        }
        else {
            // 日志队列满了，就丢弃日志
            // 这是一种设计上的选择，宁可丢弃，也不要阻塞
            m_dropped++;
        }
        m_mutex.unlock();
    }
}

// 日志类自己从队列中取并写入文件，算是消费者
void* Log::async_write_log(void* arg) 
{
    // 把当前m_log_queue中所有的log一次性写入磁盘，使用m_cond来唤醒
    Log* log = (Log*)arg; //???????????为啥要传入一个arg
    log->flush_to_file();
    return nullptr;
}

void Log::flush_to_file()
{
    while (m_running)
    {
        m_mutex.lock();
        // 如果队列为空 且 没有停止 就等 -> threadpool
        // 如果日志数组为空 且 没有停止 就等 -> log
        while (m_queue_count == 0 && m_running) {
            m_cond.wait(m_mutex.get());
        }

        while (m_queue_count > 0) {
            char* str = m_log_queue[m_queue_front]; // 这里每一个日志都是由strdup来的，用malloc分配内存
            m_queue_front = (m_queue_front + 1) % m_max_queue_size;
            m_queue_count--;
            
            m_mutex.unlock();
            /*
            这和线程池 run() 里取任务后解锁再 request->process()                             
            是同一个道理——只在操作共享数据时持锁，处理时放锁。
            ?????????????????
            由于fputs是磁盘IO，是一个需要很久时间的过程，这里先解锁
            */
            fputs(str, m_fp);
            free(str); // 所以这里要free掉，str传递了malloc的指针

            m_mutex.lock();
        }
        fflush(m_fp); // 写入磁盘
        m_mutex.unlock();
    }
}

void Log::shutdown()
{
    if (m_running) {
        m_running = false;
        m_cond.broadcast(); // 这里使得flush_to_file中的第一个while停止，把剩余日志写入磁盘
    }

    m_mutex.lock();
    if (m_log_queue) {
        while (m_queue_count > 0) {
            char* str = m_log_queue[m_queue_front]; // 这里每一个日志都是由strdup来的，用malloc分配内存
            m_queue_front = (m_queue_front + 1) % m_max_queue_size;
            m_queue_count--;

            m_mutex.unlock();
            fputs(str, m_fp);
            free(str);
            m_mutex.lock();
        }
        delete[] m_log_queue;
        m_log_queue = nullptr;
    }
    if (m_fp) {
        if (m_dropped > 0)
            fprintf(m_fp, "[WARN] %d log entries dropped (queue full)\n", m_dropped);
        fflush(m_fp);
        fclose(m_fp);
        m_fp = nullptr;
    }
    m_mutex.unlock();
}