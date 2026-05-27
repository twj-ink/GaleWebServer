/* log.h
 * 
 * author: GaleInk
 * date: 2026/05/22
 */

#ifndef LOG_H
#define LOG_H

#include "locker.h"
#include <cstdio>
#include <cstdarg>
#include <pthread.h>

enum LogLevel { LOG_INFO = 1, LOG_WARN = 2, LOG_ERROR = 3 };  

class Log
{
private:
    Log();
    ~Log();

    // 异步后台线程
    static void* async_write_log(void* arg);

    // 实际写文件
    void flush_to_file();

    FILE* m_fp; // 日志文件指针
    int m_mode; // 0-同步，1-异步
    locker m_mutex; // 保护文件指针
    
    cond m_cond; // 条件变量，异步模式下通知后台线程有日志了

    // 异步阻塞队列
    char** m_log_queue; // 日志字符串数组，每个元素是一个日志字符串
    int m_max_queue_size;
    int m_queue_count; // 当前队列中日志数量
    int m_queue_front; // 队头索引
    int m_queue_rear; // 队尾索引
    int m_dropped; // 异步队列满时丢弃的日志数
    pthread_t m_async_thread; // 后台线程id
    bool m_running; // 后台线程是否运行

public:
    static Log* get_instance();
    // 异步需要指定队列大小
    bool init(const char* file_name, int mode, int max_queue_size = 0);
    void shutdown();
    // 用户线程构造日志信息
    void write_log(LogLevel level, const char* file, int line, const char* format, ...);
};

// 方便调用的宏                                                              
#define LOG_INFO(fmt, ...)  Log::get_instance()->write_log(LOG_INFO, \
__FILE__, __LINE__, fmt, ##__VA_ARGS__)                                      
#define LOG_WARN(fmt, ...)  Log::get_instance()->write_log(LOG_WARN, \
__FILE__, __LINE__, fmt, ##__VA_ARGS__)                                      
#define LOG_ERROR(fmt, ...) Log::get_instance()->write_log(LOG_ERROR,\
__FILE__, __LINE__, fmt, ##__VA_ARGS__)    

#endif /*LOG_H*/