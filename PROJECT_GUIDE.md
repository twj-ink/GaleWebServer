# C++ 轻量级 Web 服务器 — 从零到一完整指南

## 项目概述

本项目实现一个 Linux 下 C++ 轻量级 Web 服务器，核心特性：
- **并发模型**: 线程池 + 非阻塞 socket + epoll (ET/LT) + Reactor/Proactor
- **HTTP**: 状态机解析 GET/POST 请求
- **数据库**: MySQL 用户注册、登录
- **文件服务**: 图片、视频等静态资源
- **日志**: 同步/异步日志系统
- **性能**: Webbench 上万并发连接

---

## 前置知识清单（按学习顺序）

在开始写代码前，建议按此顺序补齐基础：

1. **Linux 网络编程基础**: socket / bind / listen / accept / connect / send / recv
2. **I/O 多路复用**: select → poll → epoll 的进化，水平触发(LT)和边缘触发(ET)的区别
3. **多线程编程**: pthread_create / pthread_join / 互斥锁 / 条件变量 / 信号量 / RAII 锁
4. **HTTP 协议**: 请求报文格式、响应报文格式、GET vs POST、常见状态码
5. **有限状态机**: 用状态机解析不定长 HTTP 报文
6. **数据库编程**: MySQL C API 基本操作 (连接、查询、获取结果)
7. **设计模式**: Reactor 模式、Proactor 模式、单例模式、RAII

---

## 项目分阶段流程

### 阶段 1: 项目骨架 — 编译通过的最小框架

**目标**: 项目能编译，Makefile 正确工作，基本类结构有定义。

**要学的东西**:
- Makefile 编写（源文件列表、依赖关系、链接参数 `-lpthread -lmysqlclient`）
- `#ifndef` / `#define` / `#endif` 头文件保护
- C++ 类的声明与实现分离

**需要实现的代码**:

```
webserver-cpp/
├── main.cpp            # 入口，解析命令行 → 初始化 WebServer → 启动
├── include/
│   ├── webserver.h     # WebServer 类声明
│   ├── threadpool.h    # 线程池类声明
│   ├── cmdline.h       # 命令行参数解析声明
│   └── locker.h        # RAII 互斥锁封装
├── webserver.cpp       # WebServer 实现
├── cmdline.cpp         # 命令行解析实现
├── threadpool.cpp      # 线程池实现
└── Makefile            # 构建
```

**关键点**:
- `main.cpp` 调用 `Cmdline` 解析参数（端口、日志模式、触发模式、数据库连接信息等）
- `WebServer::init()` 初始化所有模块后调用 `eventLoop()`

**常见 bug**:
- Makefile 拼写错误（`LDFLAGS` 写成 `LDFLAFS`）
- Makefile 的链接命令忘了加源文件 `$^`
- 头文件缺少 `#pragma once` 或 include guard，导致重复定义

**面试可能问**:
- Makefile 怎么写？编译参数 `-std=c++17 -O2 -Wall -Wextra` 各是什么意思？
- `#pragma once` vs `#ifndef` 头文件保护的区别？

---

### 阶段 2: 线程池 — 最核心的并发基础设施

**目标**: 实现一个可复用的线程池，支持添加任务和优雅关闭。

**核心概念**:
- **任务队列**: 用 `std::list<T*>` 或自行实现的链表存储待执行任务
- **工作线程**: N 个 pthread 线程，循环从任务队列取任务执行
- **同步机制**: `pthread_mutex_t` 保护队列 + `pthread_cond_t` 用于生产者-消费者通知
- **优雅关闭**: 析构时通知所有线程退出，等它们都结束再销毁资源

**要学的东西**:
- `pthread_create` / `pthread_join` 的使用
- `pthread_mutex_lock` / `pthread_mutex_unlock` 互斥访问
- `pthread_cond_wait` / `pthread_cond_signal` / `pthread_cond_broadcast` 条件变量
- 伪唤醒(spurious wakeup): `pthread_cond_wait` 必须放 while 循环中检查条件
- C++ RAII 思想: 构造获取锁，析构释放锁

**需要实现的代码**:

```cpp
// threadpool.h
template <typename T>
class threadpool {
public:
    threadpool(int thread_num = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T* request);  // 向任务队列添加任务

private:
    static void* worker(void* arg);  // 工作线程入口（必须是static）
    void run();                      // 实际工作循环

    int m_thread_num;                // 线程数量
    pthread_t* m_threads;            // 线程数组
    std::list<T*> m_workqueue;       // 任务队列
    pthread_mutex_t m_mutex;         // 互斥锁
    pthread_cond_t m_cond;           // 条件变量
    bool m_stop;                     // 停止标志
};
```

**关键点**:
- `pthread_create` 的第三个参数必须是 static 函数，不能是非 static 成员函数（C++ 成员函数有隐式 this 指针，和 pthread 要求的签名不匹配）。static 函数中通过 arg 传 this 指针再调用 `run()`
- `pthread_cond_wait(&m_cond, &m_mutex)` 在被唤醒后**可能**是伪唤醒，因此要用 `while` 不是 `if` 判断条件
- 析构函数中先置 `m_stop = true`，再 `pthread_cond_broadcast` 唤醒所有线程，最后逐个 `pthread_join`

**常见 bug**:
| bug | 原因 | 修复 |
|-----|------|------|
| 线程无法退出 | `pthread_cond_wait` 中用 `if` 而非 `while`，伪唤醒后直接往下走 | `if` → `while` |
| 程序崩溃在析构 | 线程还在跑，主线程已经 delete 了任务队列 | 析构时 broadcast → join 等所有线程结束 |
| 锁忘记释放 | 某个分支 return 前没 unlock | 使用 RAII 锁封装 |

**面试可能问**:
- 线程池的核心原理？如何保证线程安全？
- 条件变量为什么必须配合 while 循环使用？（伪唤醒）
- 任务队列满了怎么办？（生产者-消费者、限流策略）
- `pthread_cond_signal` vs `pthread_cond_broadcast` 的区别？

---

### 阶段 3: RAII 锁封装 — 避免死锁和忘记释放

**目标**: 用 C++ 构造/析构自动管理锁的获取和释放。

**要学的东西**:
- RAII (Resource Acquisition Is Initialization) 惯用法
- 互斥锁、信号量、条件变量的封装

**需要实现的代码**:

```cpp
// locker.h
class locker {
public:
    locker()  { pthread_mutex_init(&m_mutex, NULL); }
    ~locker() { pthread_mutex_destroy(&m_mutex); }
    bool lock()   { return pthread_mutex_lock(&m_mutex) == 0; }
    bool unlock() { return pthread_mutex_unlock(&m_mutex) == 0; }
    pthread_mutex_t* get() { return &m_mutex; }
private:
    pthread_mutex_t m_mutex;
};

class sem {
public:
    sem()  { sem_init(&m_sem, 0, 0); }
    ~sem() { sem_destroy(&m_sem); }
    bool wait() { return sem_wait(&m_sem) == 0; }
    bool post() { return sem_post(&m_sem) == 0; }
private:
    sem_t m_sem;
};

// RAII自动加锁/解锁
class scope_lock {
public:
    scope_lock(locker* lk) : m_locker(lk) { m_locker->lock(); }
    ~scope_lock() { m_locker->unlock(); }
private:
    locker* m_locker;
};
```

**常见 bug**:
- 忘记调用 `pthread_mutex_destroy` 导致内存泄漏
- 异常路径上锁未释放 → 死锁。RAII 锁是解药

**面试可能问**:
- 什么是 RAII？用在哪里？
- 有异常抛出时 RAII 锁能保证释放吗？（能，因为析构函数一定会被调用）

---

### 阶段 4: epoll I/O 多路复用 — 高性能核心

**目标**: 用 epoll 统一管理所有客户端的 I/O 事件，支持 LT 和 ET 两种模式。

**核心概念**:
- **epoll_create**: 创建 epoll 实例，返回 fd
- **epoll_ctl**: 添加/修改/删除监听的文件描述符
- **epoll_wait**: 等待就绪事件，返回事件数组
- **LT (Level Trigger, 水平触发)**: 只要 socket 可读/可写就一直通知，没读完下次还会通知
- **ET (Edge Trigger, 边缘触发)**: 只在状态变化时通知一次，必须一次性读完/写完。必须配合非阻塞 fd 使用

**要学的东西**:
- `epoll_create` / `epoll_ctl` / `epoll_wait` 的 API 用法
- `EPOLLIN` / `EPOLLOUT` / `EPOLLET` / `EPOLLONESHOT` / `EPOLLRDHUP` 事件标志含义
- LT 和 ET 的本质区别：状态通知 vs 边沿通知
- `EPOLLONESHOT`: 一个 socket 连接在任一时刻只被一个线程处理
- 阻塞 fd vs 非阻塞 fd (`fcntl(fd, F_SETFL, O_NONBLOCK)`)

**需要实现的代码**:

```cpp
// include/epoller.h
class Epoller {
public:
    Epoller(int max_events = 10000);
    ~Epoller();
    bool add_fd(int fd, uint32_t events, bool one_shot = true);
    bool mod_fd(int fd, uint32_t events, bool one_shot = true);
    bool del_fd(int fd);
    int wait(int timeout_ms = -1);
    int get_event_fd(int i) const;
    uint32_t get_events(int i) const;

private:
    int m_epollfd;
    struct epoll_event* m_events;  // epoll_wait 返回的事件数组
    int m_max_events;
};
```

**关键点 — LT vs ET 实战**: 
- **ET 下 `read` 必须循环读直到返回 `EAGAIN`**（表示缓冲区已空）
- ET 必须设置 fd 为非阻塞（否则 read 可能阻塞住，导致其他连接饿死）
- ET 比 LT 效率高（减少 epoll_wait 的触发次数），但实现更复杂
- 监听 socket 用 LT（接受新连接更安全），通信 socket 用 ET

**常见 bug**:
| bug | 原因 | 修复 |
|-----|------|------|
| ET 模式丢数据 | 没循环读直到 EAGAIN | while(read) 直到 errno==EAGAIN |
| ET 模式阻塞卡死 | fd 没设为非阻塞 | `fcntl(fd, F_SETFL, O_NONBLOCK)` |
| 连接断开后读取 CPU 100% | ET 下对端关闭，EPOLLIN 持续触发 | 检查 read 返回 0（对端关闭），及时 close |
| 新连接丢失 | listen fd 没有在 ET 下循环 accept 直到 EAGAIN | while(accept) 直到 EAGAIN |

**面试必问题**:
- epoll 为什么比 select/poll 快？
  - select/poll 每次都要把 fd_set 从用户态拷贝到内核态
  - select 有 fd 数量限制（1024），epoll 无限制
  - epoll 用红黑树 + 就绪链表，事件驱动 O(1) 复杂度
- LT 和 ET 的区别？什么时候用哪个？
- EPOLLONESHOT 的作用？
- `epoll_wait` 返回后 ET 下如何确保数据读完整了？

---

### 阶段 5: HTTP 状态机 — 解析 HTTP 请求报文

**目标**: 用有限状态机分阶段解析 HTTP 请求（请求行 → 请求头 → 请求体）。

**核心概念**:
- HTTP 请求报文格式:
  ```
  GET /index.html HTTP/1.1\r\n          ← 请求行
  Host: localhost:8080\r\n               ← 请求头
  Connection: keep-alive\r\n
  \r\n                                   ← 空行（头/体分隔）
  username=admin&password=123456         ← 请求体 (仅 POST)
  ```
- **状态机状态**: 解析请求行 → 解析请求头 → 解析请求体 → 完成
- **主/从状态机**: 主状态机决定当前在读报文哪一部分，从状态机处理每行的具体解析

**要学的东西**:
- HTTP 请求/响应报文完整格式
- GET 和 POST 的区别（GET 参数在 URL，POST 在请求体）
- 常见 HTTP 状态码: 200 OK, 400 Bad Request, 404 Not Found, 500 Internal Server Error
- 有限状态机设计模式
- 字符串查找 `strstr` / `strpbrk` / `strchr` 的使用

**需要实现的代码**:

```cpp
// 请求行
struct RequestLine {
    string method;   // GET / POST
    string url;
    string version;  // HTTP/1.1
};

// HTTP解析状态
enum HTTP_CODE {
    NO_REQUEST,          // 请求不完整，继续读
    GET_REQUEST,         // 获得完整GET请求
    BAD_REQUEST,         // 报文错误
    INTERNAL_ERROR,      // 服务器内部错误
};

enum PARSE_STATE {
    PARSE_REQUESTLINE,   // 正在解析请求行
    PARSE_HEADER,        // 正在解析请求头
    PARSE_BODY,          // 正在解析请求体
};

class HttpConn {
public:
    void init(int sockfd, const sockaddr_in& addr);
    void process();      // 入口：读数据 + 解析
    bool read_once();    // 从 fd 读入缓冲区
    HTTP_CODE parse_request(); // 状态机解析

private:
    int m_sockfd;
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;      // 已读入缓冲区的字节数
    int m_checked_idx;   // 已解析的字节数
    int m_start_line;    // 当前行起始位置
    PARSE_STATE m_state; // 主状态
    // ...
};
```

**状态机图示**:
```
PARSE_REQUESTLINE → PARSE_HEADER → PARSE_BODY → GET_REQUEST
       ↓                 ↓              ↓
    BAD_REQUEST      BAD_REQUEST    BAD_REQUEST
```

**解析流程**:
1. `read_once()`: 用 `recv()` 从 socket 读数据到 `m_read_buf`
2. `parse_request()`: 主状态机每轮读取一行
   - 从状态机: `parse_line()` 解析出一行（以 `\r\n` 结尾）
   - 主状态机: 根据当前状态决定如何处理这一行

**常见 bug**:
| bug | 原因 | 修复 |
|-----|------|------|
| 中文/特殊字符乱码 | URL 编码未处理 | decode URL（%20 → 空格） |
| 大文件 POST 解析不完整 | 一次 read 读不完请求体 | 根据 Content-Length 读满 |
| 连接一直不释放 | keep-alive 下没有超时机制 | 加定时器断开超时连接 |
| 缓冲区溢出 | read 时没有边界检查 | 检查 `m_read_idx < READ_BUFFER_SIZE` |

**面试可能问**:
- HTTP 报文格式？GET 和 POST 有什么区别？
- 为什么用状态机解析 HTTP？（TCP 是字节流，一次 read 可能只读到半个请求，需要分阶段处理不完整的报文）
- keep-alive 是什么？怎么实现？

---

### 阶段 6: Reactor / Proactor 事件处理模式

**目标**: 理解并实现两种事件处理模型，知道各自的适用场景。

**核心概念**:

**Reactor（同步 I/O）**:
```
epoll_wait 返回就绪事件
    → 主线程/通知工作线程 读取 socket 数据（同步 I/O，可能阻塞当前线程一小段时间）
    → 数据处理（业务逻辑）
    → 发送响应
```
- I/O 和业务逻辑在同一个线程里完成
- 主线程只负责监听 + 分发事件，工作线程负责读写 + 业务

**Proactor（异步 I/O）**:
```
主线程发起异步 I/O 并注册回调
    → 内核完成 I/O 后通知
    → 主线程调用回调处理数据（数据已经在缓冲区里了）
```
- I/O 由内核完成，工作线程拿到的是已经读好的数据
- 本项目中用**模拟 Proactor**: 主线程负责 epoll_wait + 读数据，然后把已读好的数据交给工作线程处理业务

**本项目两种实现方式**:

| | Reactor | 模拟 Proactor |
|---|---|---|
| epoll_wait 谁做 | 主线程 | 主线程 |
| I/O 读谁做 | 工作线程 | 主线程 |
| 业务处理谁做 | 工作线程 | 工作线程 |
| 工作线程拿到什么 | 就绪的 fd（自己读） | 已读好的请求数据 |

**代码关键差异**:
- Reactor: 线程池的 `append()` 参数是一个 `HttpConn*`，线程拿到后自己调 `read_once()` + `process()`
- 模拟 Proactor: 主线程先 `read_once()` 把数据读好，再把 `HttpConn*` 放进线程池，线程只做业务处理

**面试必问题**:
- Reactor 和 Proactor 的区别是什么？
- 你项目中的 Proactor 为什么是"模拟"的？
- 主线程和线程池之间怎么交互？（epoll + 任务队列）
- 这两种模式分别适合什么场景？

---

### 阶段 7: 定时器 — 关闭长时间不活跃的连接

**目标**: 用最小堆或升序链表管理定时器，定时断开超时连接。

**核心概念**:
- 每个连接有一个定时器（记录过期时间）
- 每次有活动（收到数据）就重置对应定时器
- 主循环用 `epoll_wait` 的超时参数替代 `SIGALRM` 信号触发定时检查
- 超时的连接 → 从 epoll 移除 → close

**要学的东西**:
- 最小堆数据结构（用 `std::priority_queue` 或手写小顶堆）
- 升序双向链表作为定时器容器（更简单，适合定时器场景）
- `time()` / `alarm()` / `setitimer()`
- `SIGALRM` 信号 vs epoll_wait 超时的取舍

**数据结构**:
```cpp
struct TimerNode {
    time_t expire;
    HttpConn* conn;
    TimerNode* prev;
    TimerNode* next;
};
// 升序链表：每次插入按 expire 排序，链表头部是最快超时的
// 每次 tick: 从头部开始检查，拿走所有过期节点
```

**面试可能问**:
- 为什么不用信号？信号处理函数里能做什么不能做什么？
- 定时器怎么做到 O(1) 删除指定节点？
- 时间轮(time wheel)是什么？有什么优势？

---

### 阶段 8: 数据库连接池 (MySQL) — 用户注册、登录

**目标**: 用 RAII + 连接池管理 MySQL 连接，实现用户注册和登录 API。

**核心概念**:
- 数据库连接是昂贵资源，不能每次请求都新建/销毁
- **连接池 (Connection Pool)**: 预先创建 N 个连接，需要时借一个，用完还回去
- 连接池用信号量实现生产者-消费者（没有可用连接时阻塞等待）
- RAII 封装连接的借出和归还

**要学的东西**:
- MySQL C API: `mysql_init` / `mysql_real_connect` / `mysql_query` / `mysql_store_result` / `mysql_fetch_row`
- 连接池原理和实现（懒创建 vs 预创建）
- SQL 注入的防范（用 `mysql_real_escape_string` 或参数化语句）

**需要实现的代码**:

```cpp
// include/sql_connection_pool.h
class ConnectionPool {
public:
    static ConnectionPool* getInstance();  // 单例
    void init(string host, string user, string passwd, string db,
              int port, int max_conn);
    MYSQL* getConnection();    // 借一个连接（无可用时阻塞）
    void releaseConnection(MYSQL* conn);  // 归还连接

private:
    list<MYSQL*> m_conn_list;  // 空闲连接
    locker m_lock;
    sem m_sem;                 // 信号量记录可用连接数
    int m_max_conn;
    int m_cur_conn;
};

// RAII 自动借还
class ConnectionRAII {
public:
    ConnectionRAII(MYSQL** conn, ConnectionPool* pool);
    ~ConnectionRAII();  // 析构时自动归还
};
```

**数据库表设计**:
```sql
CREATE TABLE users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) NOT NULL UNIQUE,
    password VARCHAR(100) NOT NULL  -- 存 hash 后的密码
);
```

**注册/登录逻辑**:
```
POST /register  → 解析 body 中的 username & password → INSERT → 返回 JSON
POST /login     → 解析 body → SELECT 验证 → 返回 JSON + 设置 Cookie/Session
```

**常见 bug**:
| bug | 原因 | 修复 |
|-----|------|------|
| 内存泄漏 | `mysql_store_result` 没调 `mysql_free_result` | 查所有路径确保释放 |
| 连接泄漏 | 借出后异常路径未归还 | RAII 封装（析构自动还） |
| 查询失败 | 中文编码问题 | `mysql_set_character_set(conn, "utf8")` |
| 程序退出卡死 | 线程持有连接但信号量已销毁 | 先通知所有连接归还再析构连接池 |

**面试可能问**:
- 连接池为什么用单例模式？
- RAII 怎么保证连接一定能归还？（异常安全）
- 怎么保证密码安全？(不存明文，用 bcrypt/scrypt hash)
- SQL 注入是什么？怎么防？

---

### 阶段 9: 同步/异步日志系统

**目标**: 实现可切换的同步/异步日志，记录服务器运行状态。

**核心概念**:
- **同步日志**: 每写一条日志 = 调一次 `fputs` → `fflush`。调用者阻塞等待 IO 完成
- **异步日志**: 日志先放进缓冲区 → 唤醒日志线程 → 日志线程批量写文件。调用者不阻塞
- **生产者-消费者**：业务线程是生产者，日志线程是消费者
- 用**双缓冲**减少锁竞争：日志线程在写 buffer A 时，业务线程往 buffer B 写

**设计**:
```
业务线程 → push_log(buffer) → 当前写缓冲区 → (满了唤醒后台线程) 
                                                  ↓
后台日志线程 → 交换缓冲区 → 写盘 → 清空
```

**需要实现的代码**:
```cpp
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3

// 同步模式：直接写
class SyncLog {
    void write_log(int level, const char* format, ...);
};

// 异步模式：用阻塞队列 + 后台线程批量写
class AsyncLog {
    void write_log(int level, const char* format, ...);  // 放入队列
    static void* flush_thread(void* arg);                 // 后台刷盘线程
    // ...
};
```

**日志格式示例**:
```
[2026-05-11 15:30:45][INFO][main.cpp:23] server started on port 8080
[2026-05-11 15:30:46][ERROR][http_conn.cpp:156] parse request line failed: GET /bad url HTTP/1.1
```

**面试可能问**:
- 同步和异步日志的区别？优劣？
- 异步日志为什么快？瓶颈在哪？
- 怎么保证日志不丢失？(程序崩溃时刷缓冲区)

---

### 阶段 10: 整合与压力测试

**目标**: 把所有模块串联起来，用 Webbench 验证性能。

**集成流程**:
1. `main()` 解析命令行 → 初始化日志、连接池、线程池
2. 创建监听 socket + epoll → 注册 listen fd
3. `eventLoop()`: `epoll_wait()` → 对新连接 accept + 注册读事件 → 对已有连接读/写/错误处理
4. 每个 HTTP 请求: 主线程读(Proactor)或工作线程读(Reactor) → 状态机解析 → 路由分发 → 生成响应 → 注册写事件
5. 写完成 → 决定 keep-alive 保持连接 或 close

**内存中的核心数据结构关系**:
```
main()
  ├── 单例: ConnectionPool::getInstance()   // 数据库连接池
  ├── 单例: Log::getInstance()              // 日志
  ├── Epoller epoll                         // epoll 封装
  ├── threadpool<HttpConn> pool             // 线程池
  └── HttpConn users[MAX_FD]               // 每个 fd 对应一个 HttpConn 对象
```

**测试**:
```bash
webbench -c 10000 -t 60 http://localhost:8080/
```

**常见性能瓶颈**:
- 数据库查询慢 → 检查索引、使用连接池
- 线程竞争激烈 → 减少锁粒度
- 内存分配频繁 → 预分配/对象池
- 文件 I/O 阻塞 → 用 sendfile 零拷贝

**面试可能问**:
- 你怎么测试性能？Webbench 原理是什么？
- 能支持多少并发？瓶颈在哪？
- 怎么优化到更高？

---

## 面试总复习提纲

| 模块 | 必问问题 | 你应能回答 |
|------|---------|-----------|
| epoll | 为什么比 select/poll 快？ | 红黑树+就绪链表, 无 fd 限制, O(1) |
| epoll | LT vs ET 区别？ | 状态触发 vs 边沿触发; ET 必须非阻塞+循环读 |
| 线程池 | 怎么工作？ | N 个线程, 1 个任务队列, mutex+cond 同步 |
| 线程池 | 怎么安全关闭？ | stop 标志 + broadcast + join |
| HTTP | 为什么用状态机？ | TCP 字节流, 一次 read 可能不完整 |
| Reactor vs Proactor | 区别？ | 同步 I/O vs 异步 I/O; 谁负责读 |
| 数据库 | 连接池原理？ | 预创建, 借还模型, RAII |
| 数据库 | 密码安全？ | hash 存储, SQL 注入防护 |
| 日志 | 同步 vs 异步？ | 异步用缓冲+后台线程, 减少写盘等待 |
| C++ | RAII 是什么？ | 构造获取资源, 析构释放, 异常安全 |
| 定时器 | 怎么管理？ | 升序链表/最小堆, epoll_wait 超时触发 |
| 并发 | 怎么保证线程安全？ | mutex + scope_lock + 原子操作 |

---

## 建议的开发顺序

```
1. Makefile + 空类结构         → 编译通过
2. 线程池                       → 可测试（创建/添加任务/销毁）
3. RAII 锁封装                  → 和线程池配套
4. epoll 封装                   → 可测试（创建/添加 fd/等待/删除）
5. 监听 socket + accept 连接    → 能接受浏览器连接
6. HTTP 状态机                  → 能解析浏览器发来的请求
7. Reactor/Proactor 主循环      → 能返回简单响应（如 Hello World）
8. 静态文件服务                 → 能返回 HTML/图片
9. 定时器                       → 断开超时连接
10. 数据库连接池 + 注册/登录     → 用户系统
11. 日志系统                    → 记录运行日志
12. Webbench 压力测试 + 优化    → 性能调优
```

每个阶段完成后编译测试通过再进入下一阶段。
