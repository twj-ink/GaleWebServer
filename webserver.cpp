/* webserver.cpp
 * 
 * author: GaleInk
 * date: 2026/05/21
 */


#include "include/webserver.h"
#include "include/http_conn.h"
#include "include/timer.h"
#include "include/log.h"
#include <asm-generic/socket.h>
#include <sys/socket.h>                                                 
#include <netinet/in.h>                                                 
#include <arpa/inet.h>                                                  
#include <unistd.h>
#include <cstring>

WebServer::WebServer()
{
    m_port = 0;
    m_listenfd = -1;
    m_users = nullptr;
}

WebServer::~WebServer()
{
    if (m_listenfd != -1) close(m_listenfd);
    if (m_users != nullptr) delete[] m_users;
}

void WebServer::init(int port)
{
    m_port = port;
    m_users = new HttpConn[MAX_FD];
    m_timer_nodes = new TimerNode[MAX_FD];
    init_socket();
    m_epoller.add_fd(m_listenfd, EPOLLIN, false);
}

// 创建监听socket
bool WebServer::init_socket()
{   
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenfd < 0)
    {
        LOG_ERROR("socket failed: %s", strerror(errno));
        return false;
    }

    int opt = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);

    if (bind(m_listenfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        LOG_ERROR("bind failed: %s", strerror(errno));
        return false;
    }
    if (listen(m_listenfd, 65535) < 0)
    {
        LOG_ERROR("listen failed: %s", strerror(errno));
        return false;
    }
    /*
    listen创建了1024的accept队列，当有新连接来的时候，完成三次握手，
    就会放进这个队列中，此时epoll会检测到这个监听fd可读了，在epoll_wait中就会返回这个事件
    从而从队列中取出一个连接进行accept
    */

    return true;
}

void WebServer::eventLoop()
{
    while (true)
    {
        // 定时器的tick
        int timeout = m_timer_list.get_next_timeout();
        // 这个timeout是第一个要超时的连接，一开始wait的参数是-1表示无限等待，
        // 现在改成timeout，表示最多等这么久，如果在这段时间内有事件来了，就提前返回处理事件，
        // 如果没有事件来了，就等到timeout时间到了，返回0，eventLoop就会调用tick去处理超时的连接
        // int n = m_epoller.wait(); // 阻塞的等待事件
        int n = m_epoller.wait(timeout); // 阻塞的等待事件
        // 定时器处理超时连接
        m_timer_list.tick();
        /*
        epoll_wait(m_epollfd, m_events, m_max_events, timeout_ms)
        返回的是待处理事件的数量，
        The "events" parameter is a buffer that will contain triggered events
        也就是说，内核会自动把就绪的事件放到m_events当中，
        那用户需要做的就是用返回的数量n来遍历这个数组，处理每一个事件就行了。
        每一个事件都是一个struct epoll_event，包含fd和具体的事件类型（读写）
        用那俩自定义函数就行
        */
        for (int i = 0; i < n; i++) {
            int fd = m_epoller.get_event_fd(i);
            uint32_t events = m_epoller.get_events(i);

            if (fd == m_listenfd)
            /*
            现在监听端已经准备好读数据了，说明有新连接三次握手就绪了，可以accept
            */
            {
                struct sockaddr_in client_addr;
                socklen_t len = sizeof(client_addr);
                int connfd = accept(m_listenfd, (struct sockaddr*)&client_addr, &len);
                if (connfd < 0)
                {
                    LOG_ERROR("accept failed: %s", strerror(errno));
                    continue;
                }

                LOG_INFO("[CONN] new connection: fd=%d, from %s:%d\n",
                       connfd, inet_ntoa(client_addr.sin_addr),
                       ntohs(client_addr.sin_port));

                m_users[connfd].init(connfd, client_addr);
                m_users[connfd].set_timer(&m_timer_nodes[connfd]);
                m_timer_list.add_timer(&m_timer_nodes[connfd]);
                m_epoller.add_fd(connfd, EPOLLIN);// 监听读事件
            }
            else if (events & EPOLLIN)
            {
                if (!m_users[fd].read_once())
                {
                    // m_epoller.del_fd(fd, 0);
                    cleanup_conn(fd);
                    continue;
                }
                // m_users[fd].process();
                // 上面这个是串行，这里改成用线程池
                // 定时器
                m_timer_list.adjust_timer(m_users[fd].get_timer());
                m_pool.append(&m_users[fd]);
            }
        }
    }
}

void WebServer::cleanup_conn(int fd)
{
    m_epoller.del_fd(fd, 0);
    m_users[fd].close_conn();
    m_timer_list.del_timer(m_users[fd].get_timer());
}