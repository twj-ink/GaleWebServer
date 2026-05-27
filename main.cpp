/* main.cpp
 * 
 * author: GaleInk
 * date: 2026/04/03 22:05
 */

#include "include/cmdline.h"
#include "include/http_conn.h"
#include "include/threadpool.h"
#include "include/epoller.h"
#include "include/webserver.h"
#include "include/log.h"
#include <sys/socket.h>
#include <cstring>

int main(int argc, char* argv[]) {

/*
    // 测试线程池正常工作
    threadpool<testtask> pool(3, 10);
    for (int i = 0; i < 10; i++) {
        testtask* task = new testtask(i);
        pool.append(task);
    }
    sleep(5);
*/

/*
    // 测试HttpConn — 完整请求→响应闭环
    int fd[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
    // fd[0] → 服务端(HttpConn)    fd[1] → 模拟浏览器

    // 1. 模拟浏览器发请求
    const char* raw_request =
        "GET /index.html HTTP/1.1\r\n"
        "HOST: localhost\r\n"
        "Content-Length: 0\r\n"
        "\r\n";
    send(fd[1], raw_request, strlen(raw_request), 0);

    // 2. HttpConn 读 + 解析 + 响应
    struct sockaddr_in addr;
    HttpConn conn;
    conn.init(fd[0], addr);
    conn.read_once();
    conn.process();    // parse_request + write_response

    // 3. 从浏览器端读响应并打印
    char response[2048] = {0};
    recv(fd[1], response, sizeof(response) - 1, 0);
    printf("=== Server Response ===\n%s\n", response);

    close(fd[0]);
    close(fd[1]);
    return 0;
*/

    Cmdline cmd;
    cmd.parse_args(argc, argv);

    // init Log
    if (cmd.CloseLog == 0) {
        // 开日志
        Log::get_instance()->init("./server.log", cmd.LogWrite, 1024);
    }

    WebServer server;
    server.init(cmd.Port);
    server.eventLoop();
    return 0;

}