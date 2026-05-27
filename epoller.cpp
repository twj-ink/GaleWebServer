/* epoller.cpp
 * 
 * author: GaleInk
 * date: 2026/05/13
 */

#include "include/epoller.h"
#include <cstddef>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

Epoller::Epoller(int max_events)
{
    // 构造函数
    m_max_events = max_events;
    m_epollfd = epoll_create(1);
    m_events = new epoll_event[max_events];
}

Epoller::~Epoller()
{
    close(m_epollfd);
    delete[] m_events;
}

/*
  ET 模式下，read 必须一直读到返回 EAGAIN（缓冲区空了）。如果
  fd 是阻塞模式，读到空时会卡住整个线程，其他连接全饿死。  
*/
static void set_nonblocking(int fd)
{
    int old_flags = fcntl(fd, F_GETFL, 0); // 取当前flags
    fcntl(fd, F_SETFL, old_flags | O_NONBLOCK); // 加上非阻塞
}

bool Epoller::add_fd(int fd, uint32_t events, bool one_shot)
{
    struct epoll_event ev;
    ev.events = events;
    if (one_shot) ev.events |= EPOLLONESHOT;
    ev.data.fd = fd;

    if (epoll_ctl(m_epollfd, EPOLL_CTL_ADD, fd,&ev) == -1) {
        return false;
    }

    set_nonblocking(fd);
    return true;
}

bool Epoller::mod_fd(int fd, uint32_t events, bool one_shot)
{
    struct epoll_event ev;
    ev.events = events;
    if (one_shot) ev.events |= EPOLLONESHOT;
    ev.data.fd = fd;

    if (epoll_ctl(m_epollfd, EPOLL_CTL_MOD, fd,&ev) == -1) {
        return false;
    }

    set_nonblocking(fd);
    return true;
}

bool Epoller::del_fd(int fd, uint32_t events, bool one_shot)
{
    struct epoll_event ev;
    ev.events = events;
    if (one_shot) ev.events |= EPOLLONESHOT;
    ev.data.fd = fd;

    if (epoll_ctl(m_epollfd, EPOLL_CTL_DEL, fd,nullptr) == -1) {
        return false;
    }

    set_nonblocking(fd);
    return true;
}

int Epoller::wait(int timeout_ms)
{
    return epoll_wait(m_epollfd, m_events, m_max_events, timeout_ms);
}

int Epoller::get_event_fd(int i) const
{
    return m_events[i].data.fd;
}
uint32_t Epoller::get_events(int i) const
{
    return m_events[i].events;
}