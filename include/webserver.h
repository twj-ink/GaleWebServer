/* webserver.h
 * 
 * author: GaleInk
 * date: 2026/05/21
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <string>
#include "epoller.h"
#include "threadpool.h"
#include "http_conn.h"
#include "timer.h"

static const int MAX_FD = 65535;

class WebServer 
{
public:
    WebServer();
    ~WebServer();

    void init(int port);
    void eventLoop();

private:
    bool init_socket();

    int m_port;
    int m_listenfd;
    Epoller m_epoller;
    threadpool<HttpConn> m_pool;
    HttpConn* m_users; // 后续用new分配数组，可以用fd直接找到对应连接

    TimerList m_timer_list;
    TimerNode* m_timer_nodes;
    void cleanup_conn(int fd);
};


#endif /*WEBSERVER_H*/