/* http_conn.cpp
 * 
 * author: GaleInk
 * date: 2026/05/13
 */

#include "include/http_conn.h"
#include "include/epoller.h"
#include "include/log.h"
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <strings.h>
#include <cstdio>
#include <cerrno>
#include <sys/stat.h>

HttpConn::HttpConn()
{
    m_sockfd = -1;
    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    m_state = PARSE_REQUESTLINE;
    m_content_length = 0;
    memset(m_method, 0, sizeof(m_method));
    memset(m_url, 0, sizeof(m_url));
    memset(m_version, 0, sizeof(m_version));
    memset(m_read_buf, 0, sizeof(m_read_buf));
}

HttpConn::~HttpConn()
{
    close(m_sockfd);
}

void HttpConn::init(int sockfd, const struct sockaddr_in& addr)
{
    m_sockfd = sockfd;
    m_address = addr;                                                                            
    m_read_idx = 0;                                                                              
    m_checked_idx = 0;                                                                           
    m_start_line = 0;                                                                            
    m_state = PARSE_REQUESTLINE;                                                                 
    m_content_length = 0;                    
    m_write_idx = 0;                                                    
    memset(m_read_buf, 0, READ_BUFFER_SIZE);                                                     
    memset(m_write_buf, 0, WRITE_BUFFER_SIZE);                                                     
    memset(m_method, 0, sizeof(m_method));                                                       
    memset(m_url, 0, sizeof(m_url));                                                             
    memset(m_version, 0, sizeof(m_version));  
}

void HttpConn::close_conn()
{
    if (m_sockfd != -1) {
        close(m_sockfd);
        m_sockfd = -1;
    }
}

// 从socket中读数据到自己的缓冲区
bool HttpConn::read_once()
{
    int bytes = recv(m_sockfd,
                     m_read_buf + m_read_idx, // 从缓冲区尾部继续写
                     READ_BUFFER_SIZE - m_read_idx, // 剩余空间
                     0);
    if (bytes == 0)
    {
        LOG_INFO("fd=%d: client closed connection", m_sockfd);
        return false;
    }
    if (bytes < 0)
    {
        LOG_ERROR("fd=%d: recv failed: %s", m_sockfd, strerror(errno));
        return false;
    }
    m_read_idx += bytes;
    LOG_INFO("fd=%d: got %d bytes:\n%.*s", m_sockfd, bytes, bytes, m_read_buf);
    return true;
}

char* HttpConn::get_line()
{
    for (int i = m_start_line; i < m_read_idx; i++) {
        if (m_read_buf[i] == '\r') {
            if (i + 1 >= m_read_idx || m_read_buf[i + 1] != '\n') {
                return nullptr; // 需要读到完整的\r\n
            }
            // 现在找到一个\r\n
            char* line = &m_read_buf[m_start_line];
            m_start_line = i + 2; // \r\n之后的
            m_read_buf[i] = '\0';
            m_read_buf[i + 1] = '\0';
            return line;
        }
    }
    return nullptr; // 没找到\r，行不完整
}

HTTP_CODE HttpConn::parse_request()
{
    char* line = nullptr;
    while ((line = get_line()) != nullptr) {
        switch (m_state)
        {
            case PARSE_REQUESTLINE:
            {
                // GET /index.html HTTP/1.1
                sscanf(line, "%7s %255s %15s", m_method, m_url, m_version);

                // 现在只支持get和post
                if (strcasecmp(m_method, "GET") != 0 && strcasecmp(m_method, "POST") != 0) {
                    LOG_WARN("fd=%d: unsupported method %s", m_sockfd, m_method);
                    return BAD_REQUEST;
                }
                m_state = PARSE_HEADER;
                break;
            }
            case PARSE_HEADER:
            {
                if (*line == '\0') {
                    // 头部分结束，检查是否有请求体
                    if (m_content_length > 0) m_state = PARSE_BODY;
                    else return GET_REQUEST;
                } else {
                    // 解析头部分
                    if (strncasecmp(line, "Content-Length:", 15) == 0) {
                        sscanf(line + 15, "%d", &m_content_length);
                    }
                }
                break;
            }
            case PARSE_BODY:
            {
                int body_len = m_read_idx - m_start_line;
                if (body_len >= m_content_length) return GET_REQUEST;
                m_checked_idx = m_start_line;
                return NO_REQUEST;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    // get_line返回NULL，数据不完整
    return NO_REQUEST;
}

void HttpConn::process()
{
    HTTP_CODE ret = parse_request();
    if (ret == GET_REQUEST) {
        LOG_INFO("fd=%d: %s %s -> 200", m_sockfd, m_method, m_url);
        write_response();
        close_conn();
    } else if (ret == NO_REQUEST) {
        // 数据不完整，重新注册 ONESHOT 等待下次数据
        m_epoller->mod_fd(m_sockfd, EPOLLIN);
    } else {
        LOG_WARN("fd=%d: BAD_REQUEST, method=%s url=%s", m_sockfd, m_method, m_url);
        close_conn();
    }
}

bool HttpConn::write_response()
{
    // // 返回一个helloworld
    // const char* body = "<html><body><h1>Hello World</h1></body></html>";
    // int body_len = strlen(body);
    // m_write_idx = snLOG_INFO(m_write_buf, WRITE_BUFFER_SIZE, 
    //     "HTTP/1.1 200 OK\r\n"                                               
    //     "Content-Type: text/html\r\n"                                       
    //     "Content-Length: %d\r\n"                                            
    //     "\r\n"                                                              
    //     "%s",                                                               
    //     body_len, body);
    // return write();
    /* 新增分别处理静态和动态文件 */
    if (strcasecmp(m_method, "GET") == 0) {
        return serve_static();
    } else if (strcasecmp(m_method, "POST") == 0) {
        return serve_dynamic();
    }
    return false;
}

bool HttpConn::write()
{
    int bytes = send(m_sockfd, m_write_buf, m_write_idx, 0);
    if (bytes < 0) return false;
    LOG_INFO("fd=%d: sent %d bytes:\n%.*s", m_sockfd, bytes, bytes, m_write_buf);
    return true;
}

bool HttpConn::serve_static()
{
    // 1. 构造文件路径
    char file_path[512];
    snprintf(file_path, sizeof(file_path), ".%s", m_url);
    // /index.html -> ./index.html

    // 2. 如果url是/，默认返回index.html
    if (strcmp(m_url, "/") == 0) {
        snprintf(file_path, sizeof(file_path), "./index.html");
    }

    // 3. 获取文件信息
    struct stat file_stat;
    if (stat(file_path, &file_stat) < 0) {
        LOG_WARN("fd=%d: 404 %s", m_sockfd, file_path);
        return send_error_page(404);
    }

    // 4. 循环读文件（大文件一次读不完）
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("fd=%d: open %s failed: %s", m_sockfd, file_path, strerror(errno));
        return send_error_page(403);
    }
    ssize_t file_size = file_stat.st_size;
    char* file_buf = new char[file_size];   // 用 new 代替 VLA
    ssize_t total_read = 0;
    while (total_read < file_size)
    {
        ssize_t n = read(fd, file_buf + total_read, file_size - total_read);
        if (n <= 0) break;
        total_read += n;
    }
    close(fd);

    // 5. 根据后缀决定content-type
    const char* mime = get_mime_type(file_path);

    // 6. 构造响应
    m_write_idx = snprintf(m_write_buf, WRITE_BUFFER_SIZE,
          "HTTP/1.1 200 OK\r\n"
          "Content-Type: %s\r\n"
          "Content-Length: %ld\r\n"
          "\r\n",
          mime, total_read);

    // 7. 发头部
    write();

    // 8. 循环发送文件内容（大文件一次 send 发不完）
    ssize_t total_sent = 0;
    while (total_sent < total_read)
    {
        ssize_t n = send(m_sockfd, file_buf + total_sent,
                         total_read - total_sent, 0);
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;         // 非阻塞模式下缓冲区满，重试
            break;                // 真正的错误
        }
        total_sent += n;
    }

    LOG_INFO("fd=%d: served %s, %ld bytes", m_sockfd, file_path, total_sent);

    delete[] file_buf;
    return true;
}

bool HttpConn::send_error_page(int code) 
{
    const char* body;
    const char* title;
    switch (code) {
        case 404:
            body = "<html><body><h1>404 Not Found</h1></body></html>";
            title = "Not Found";
            break;
        case 403:
            body = "<html><body><h1>403 Forbidden</h1></body></html>";
            title = "Forbidden";
            break;
        default:
            body = "<html><body><h1>500 Internal Server Error</h1></body></html>";
            title = "Internal Server Error";
            break;
    }
    
    m_write_idx = snprintf(m_write_buf, WRITE_BUFFER_SIZE, 
        "HTTP/1.1 %d %s\r\n"                                               
        "Content-Type: text/html\r\n"                                       
        "Content-Length: %d\r\n"                                            
        "\r\n"                                                              
        "%s",                                                               
        code, title, (int)strlen(body), body);
    return write(); // 直发头部
}

const char* HttpConn::get_mime_type(const char* path)
{
    const char* ext = strrchr(path, '.');  // 找最后一个 .              
    if (!ext) return "application/octet-stream";                        
                                                                        
    if (strcmp(ext, ".html") == 0)  return "text/html";                 
    if (strcmp(ext, ".jpg")  == 0)  return "image/jpeg";                
    if (strcmp(ext, ".png")  == 0)  return "image/png";                 
    if (strcmp(ext, ".gif")  == 0)  return "image/gif";                 
    if (strcmp(ext, ".mp4")  == 0)  return "video/mp4";                 
    if (strcmp(ext, ".css")  == 0)  return "text/css";                  
    if (strcmp(ext, ".js")   == 0)  return "text/javascript";           
    return "application/octet-stream";     
}

bool HttpConn::serve_dynamic()
{
    // 这里暂时不实现动态请求，直接返回501
    const char* body = "<html><body><h1>501 Not Implemented</h1></body></html>";
    m_write_idx = snprintf(m_write_buf, WRITE_BUFFER_SIZE, 
        "HTTP/1.1 501 Not Implemented\r\n"                                               
        "Content-Type: text/html\r\n"                                       
        "Content-Length: %d\r\n"                                            
        "\r\n"                                                              
        "%s",                                                               
        (int)strlen(body), body);
    return write();
}