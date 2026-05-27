/* timer.h
 * 
 * author: GaleInk
 * date: 2026/05/21
 */

#ifndef TIMER_H
#define TIMER_H

// #include "http_conn.h"
#include <ctime>

class HttpConn;

static const int TIMEOUT_SEC = 600; // 10分钟闹钟超时

struct TimerNode
{
    time_t expire; // 绝对过期时间
    HttpConn* conn; // 指向对应的HttpConn
    TimerNode* prev;
    TimerNode* next;
};

class TimerList
{
public:
    TimerList();
    ~TimerList();
    void add_timer(TimerNode* node);
    void adjust_timer(TimerNode* node);
    void del_timer(TimerNode* node);
    void tick(); // 驱逐一遍链表超时的连接
    int get_next_timeout() const; // 距离下一个到期还有多少ms

private:
    void add_to_tail(TimerNode* node); 
    TimerNode* m_head;
    TimerNode* m_tail;
};

#endif /*TIMER_H*/