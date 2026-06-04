/* http_conn.h
 * 
 * author: GaleInk
 * date: 2026/05/13
 */

#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <ctime>

struct TimerNode; // 前向声明，避免循环依赖
class Epoller;      // 前向声明

// 主状态机的状态    
enum PARSE_STATE {
    PARSE_REQUESTLINE,   // 正在解析请求行                                                     
    PARSE_HEADER,        // 正在解析请求头                                                     
    PARSE_BODY,          // 正在解析请求体    
};

// 解析结果                                                                                    
enum HTTP_CODE {
    NO_REQUEST,          // 请求不完整，继续读                                                 
    GET_REQUEST,         // 得到一个完整的请求                                                 
    BAD_REQUEST,         // 报文格式错误    
    INTERNAL_ERROR,                                                   
};                  

class HttpConn
{
private:
    char* get_line(); // 从缓存取一行
    int m_sockfd;
    struct sockaddr_in m_address;

    static const int READ_BUFFER_SIZE = 2048;
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;
    int m_checked_idx;
    int m_start_line;

    PARSE_STATE m_state;

    // 浏览器发来请求 → 解析 → 构建响应文本 → 写缓冲区 → 发回去
    // 解析结果
    char m_method[8];
    char m_url[256];
    char m_version[16];
    int m_content_length;

    // 响应
    static const int WRITE_BUFFER_SIZE = 2048;
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    bool write();
    bool serve_static(); // 处理静态文件请求    
    bool serve_dynamic(); // 处理动态文件请求
    bool send_error_page(int code);
    const char* get_mime_type(const char* path);

    TimerNode* m_timer;
    Epoller* m_epoller;

public:
    HttpConn();
    ~HttpConn();
    void init(int sockfd, const struct sockaddr_in& addr);
    void process(); // 线程池的线程run的部分
    bool read_once(); // 读数据
    HTTP_CODE parse_request(); // 主状态机

    bool write_response();

    void set_timer(TimerNode* timer) { m_timer = timer; }
    TimerNode* get_timer() const { return m_timer; }
    void set_epoller(Epoller* ep) { m_epoller = ep; }
    void close_conn();
    int get_sockfd() const { return m_sockfd; }

};


#endif // HTTP_CONN_H