/* epoller.h
 * 
 * author: GaleInk
 * date: 2026/05/13
 */

#include <sys/epoll.h>

#ifndef EPOLLER_H
#define EPOLLER_H

class Epoller
{
private:
    int m_epollfd; // epoll实例
/*
    typedef union epoll_data
    {
        void *ptr;
        int fd;
        uint32_t u32;
        uint64_t u64;
    } epoll_data_t;

    struct epoll_event
    {
        uint32_t events;	// Epoll events
        epoll_data_t data;	// User data variable
    } __EPOLL_PACKED;
*/
    struct epoll_event* m_events; // 事件数组，由wait填充，内核完成
    int m_max_events; // 最大事件数

public:
    Epoller(int max_events = 10000);
    ~Epoller();
    bool add_fd(int fd, uint32_t events, bool one_shot = true);
    bool mod_fd(int fd, uint32_t events, bool one_shot = true);
    bool del_fd(int fd, uint32_t events, bool one_shot = true);
    int wait(int timeout_ms = -1); // 返回需要处理的事件数

    int get_event_fd(int i) const; // 取第i个就绪fd
    uint32_t get_events(int i) const; // 取第i个就绪fd的事件类型
};

#endif // EPOLLER_H