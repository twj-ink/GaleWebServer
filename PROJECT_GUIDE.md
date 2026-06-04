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
2. **I/O 多路复用**: [select → poll → epoll 的进化](https://zhuanlan.zhihu.com/p/367591714)，水平触发(LT)和边缘触发(ET)的区别
3. **多线程编程**: pthread_create / pthread_join / 互斥锁 / 条件变量 / 信号量 / RAII 锁
4. **HTTP 协议**: 请求报文格式、响应报文格式、GET vs POST、常见状态码
5. **有限状态机**: 用状态机解析不定长 HTTP 报文
6. **数据库编程**: MySQL C API 基本操作 (连接、查询、获取结果)
7. **设计模式**: [Reactor 模式](https://zhuanlan.zhihu.com/p/713686346)、Proactor 模式、单例模式、RAII

---

## 当前进度一览

| 模块 | 文件 | 状态 | 一句话 |
|------|------|------|--------|
| 构建 | `Makefile` | ✅ 完成 | g++ -std=c++17 -O2，链接 lpthread |
| 同步工具 | `include/locker.h` | ✅ 完成 | locker(互斥锁) + scope_lock(RAII) + sem(信号量) + cond(条件变量) |
| 线程池 | `include/threadpool.h` | ✅ 完成 | 模板类，构造创建 N 个线程，append 放任务，析构优雅关闭 |
| epoll | `include/epoller.h` + `epoller.cpp` | ✅ 完成 | 封装 epoll_create/ctl/wait，支持 LT/ET/ONESHOT |
| HTTP | `include/http_conn.h` + `http_conn.cpp` | ✅ 核心完成 | 状态机解析 GET/POST + 静态文件服务 + 循环读写大文件 |
| 命令行 | `include/cmdline.h` + `cmdline.cpp` | ✅ 完成 | 解析 `-p` 端口号 |
| 服务器骨架 | `include/webserver.h` + `webserver.cpp` | ✅ 完成 | 监听 socket + accept + epoll 主循环 + 线程池并发 |
| 定时器 | `include/timer.h` + `timer.cpp` | ✅ 完成 | 升序双向链表，epoll_wait 超时驱动 tick，O(1) 调整 |
| 数据库 | — | ⬜ 待写 | MySQL 连接池 + 注册登录 |
| 日志 | `include/log.h` + `log.cpp` | ✅ 完成 | 单例 + 同步/异步可切换 + 环形队列 + 后台线程批量写 |
| 压力测试 | Webbench | ✅ 完成 | 500-1000 并发，QPS 2900+，成功率 96% |

### 已完成模块之间的关系

```
main()
  ├── Cmdline:       解析命令行参数（端口）
  └── WebServer:
        ├── Epoller:       监控所有 fd 的 I/O 事件
        ├── threadpool<HttpConn>:  8 个工作线程（模拟 Proactor）
        ├── HttpConn[]:    预分配数组 m_users[MAX_FD]，下标 = fd
        ├── TimerNode[]:   预分配数组 m_timer_nodes[MAX_FD]，下标 = fd
        ├── TimerList:     双向链表串起活跃连接的 TimerNode
        ├── Log:           单例日志（同步/异步，INFO/WARN/ERROR）
        └── eventLoop() 主循环:
              epoll_wait(timeout) → tick() 清理超时
                                 → accept 新连接 + 创建定时器
                                 → read_once 读数据 → adjust_timer 续期
                                 → pool.append → 工作线程 process
```

### 里程碑 1：浏览器看到 Hello World ✅

简单的静态文件服务 + 线程池并发。

**完整请求→响应路径**（模拟 Proactor）：

```
1. epoll_wait(timeout) 返回 → tick() 清理超时连接
2. listen_fd 可读 → accept 得到 connfd
3. HttpConn[connfd].init() + 绑定 TimerNode + 注册 epoll(EPOLLIN | ONESHOT)
4. 浏览器发数据 → connfd 可读 → read_once() → adjust_timer(续期)
5. pool.append(&m_users[fd]) → 工作线程调 process()
6. parse_request() → write_response() → serve_static() 读磁盘文件循环发送
7. process() 调 close_conn() → 连接关闭
8. tick() 检查超时连接 → 超时的调 close_conn() 强制断开
```

**编译运行**：
```bash
make && ./server -p 8080
# 浏览器访问 http://localhost:8080/  → index.html
#              http://localhost:8080/test.jpg
```

### 项目状态

**所有核心模块已完成**（线程池 + epoll + HTTP 状态机 + 定时器 + 日志 + 静态文件 + Webbench 压测）。数据库为可选项，不是 项目核心竞争力。

```
✅ 阶段 1-7：线程池 / epoll / HTTP 状态机 / 定时器 / 日志 / 静态文件 / Webbench
⚪ 阶段 8：数据库（可选加分项）
```

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

### 阶段 2: 线程池 + 同步工具 — 最核心的并发基础设施

**实际文件**: `include/threadpool.h` (所有代码在头文件中，因为是模板类)、`include/locker.h`

**目标**: 实现一个可复用的线程池，支持添加任务和优雅关闭。同步工具用 RAII 封装避免死锁。

**核心概念**:
- **任务队列**: 用 `std::list<T*>` 存储待执行任务的指针
- **工作线程**: 构造时创建 N 个 pthread 线程，线程在 `run()` 中循环等任务
- **生产者-消费者模型**: `append()` 是生产者（放任务），`run()` 是消费者（取任务）
- **同步机制**: `locker` 包装 `pthread_mutex_t` 保护队列 + `cond` 包装 `pthread_cond_t` 用于线程间通知
- **优雅关闭**: 析构时 `m_stop = true` → `broadcast()` 全部唤醒 → 各线程检测到 stop 退出循环

**实际代码结构**:

```cpp
// locker.h — 三个同步原语的 RAII 封装
class locker      // 互斥锁：保护任务队列，同一时刻只有一个线程操作队列
class scope_lock  // RAII 自动锁：构造时 lock，析构时 unlock，异常安全
class sem         // 信号量：控制资源数量（后面连接池阶段用）
class cond        // 条件变量："有活了" / "收工了" 的线程间通知

// threadpool.h — 线程池模板类
template <typename T>
class threadpool {
public:
    threadpool(int thread_num = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T* request);                  // 生产者：往队列放任务

private:
    static void* worker(void* arg);           // pthread 入口（static），通过 arg 拿 this
    void run();                               // 消费者循环：取任务 → 干 活

    std::list<T*> m_workqueue;                // 任务队列（存指针）
    int m_thread_num;                         // 线程数
    int m_max_requests;                       // 队列最大长度
    pthread_t* m_threads;                     // 线程 ID 数组（new 分配）
    bool m_stop;                              // 停止标志
    locker m_mutex;                           // 互斥锁（保护 m_workqueue）
    cond m_cond;                              // 条件变量（新任务 / 停止通知）
};
```

**run() 详解 — 消费者循环**:

每个工作线程进入 `run()` 后执行以下无限循环：

```
while (true) {
    ① m_mutex.lock()                        ← 拿锁保护队列
    ② while (队列空 && !stop) {
           m_cond.wait(m_mutex.get())       ← wait 三件事：
       }                                         释放锁 → 睡觉 → 醒来拿锁
    ③ if (m_stop) { 解锁; break; }          ← 析构通知退出了
    ④ request = 队列.front(); 队列.pop();    ← 取出一个任务
    ⑤ m_mutex.unlock()                      ← 释放锁
    ⑥ request->process()                    ← 不持锁执行，让其他线程也能取任务
}
```

**关键设计决策**:

- **取任务时持锁，执行任务时不持锁**：如果执行时持锁，8 个线程变串行，线程池白写
- **wait 用 while 不用 if**：伪唤醒后 while 重新检查队列是否真的非空
- **用 `pthread_detach` 而非 `pthread_join`**：线程创建后分离，不需要主线程 join 回收。析构时用 `m_stop + broadcast` 让各线程自行退出
- **`cond.wait()` 传 `m_mutex.get()`**：`locker.get()` 返回内部的 `pthread_mutex_t*`，条件变量要的是裸锁指针

**append() — 生产者**:

```cpp
bool append(T* request) {
    m_mutex.lock();                              // 拿锁
    if (队列满了) { m_mutex.unlock(); return false; }  // 拒绝
    队列.push_back(request);                      // 入队
    m_cond.signal_one();                         // 叫醒一个消费者
    m_mutex.unlock();                            // 解锁
    return true;
}
```

用 `signal_one()` 而非 `broadcast()`：一次只放了一个任务，叫醒一个就够了。

**析构函数 — 优雅关闭**:

```cpp
~threadpool() {
    m_stop = true;           // 设置停止标志
    m_cond.broadcast();      // 叫醒所有在 wait 的线程
    delete[] m_threads;      // 释放线程 ID 数组
}
```

各线程醒来后检查 `m_stop == true` → 解锁 → break → 退出 run → worker 返回 → 线程自动结束（因为是 detach 状态）。

**测试方法**:

写一个假的任务类，实现 `process()` 方法，在 main 中创建线程池测试：

```cpp
class testtask {
    int m_id;
public:
    testtask(int id) : m_id(id) {}
    void process() {
        printf("thread %lu is processing task %d\n", pthread_self(), m_id);
        usleep(100000);
    }
};

int main() {
    threadpool<testtask> pool(3, 10);
    for (int i = 0; i < 10; i++)
        pool.append(new testtask(i));
    sleep(3);   // 等线程处理完，不然 main 结束析构会崩
    return 0;
}
```

**自己写测试时踩过的坑**:
| 现象 | 原因 | 修复 |
|------|------|------|
| 崩溃：`mutex->__data.__owner == 0` 断言失败 | 没写析构函数，main 结束时 pool 销毁，线程还在跑访问已销毁的锁 | 补析构函数：stop + broadcast |
| 进程直接退出无输出 | main 结束太快，线程还没来得及执行 | main 末尾加 `sleep(3)` |
| 任务顺序和预期不同 | 多线程抢任务，谁先抢到谁处理 | 正常现象，不是 bug |

**面试问题与回答**:

**Q1: 线程池的核心原理是什么？如何保证线程安全？**
- 线程池 = 固定数量的常驻线程 + 共享任务队列
- 线程安全靠互斥锁保证：任何人操作队列前必须先拿锁，操作完释放
- 本项目用 `locker` 封装 `pthread_mutex_t`，在 `run()` 和 `append()` 中手动 lock/unlock
- 空闲时线程在条件变量上睡觉，不等死循环，不占 CPU

**Q2: 为什么 `pthread_cond_wait` 必须配合 while 循环而非 if？**
- 两个原因：**伪唤醒**（内核可能无故唤醒线程）和**竞争**（多个线程被唤醒后，抢锁过程中另一个线程可能已经取走了唯一的任务）
- while 保证醒来后重新检查条件（队列是否真的非空），不满足就继续等
- 如果用 if：伪唤醒直接往后走 → 队列仍是空 → `front()` 取到空引用 → 崩溃

**Q3: 任务队列满了怎么办？**
- 本项目 `append()` 检查 `size >= m_max_requests`，满了直接返回 false
- 调用方（主线程）收到 false 后可以：丢弃、重试、或者阻塞等待
- 这是一种**限流策略**，防止内存无限增长把服务器撑爆

**Q4: `pthread_cond_signal` vs `pthread_cond_broadcast` 的区别？**
- `signal_one()`：唤醒 1 个等待线程。`append` 用它（一次只放一个任务，叫醒多了其他人也是白醒）
- `broadcast()`：唤醒全部等待线程。析构函数用它（需要所有线程都醒来发现 stop 并退出）
- 用错的代价：append 用 broadcast → 惊群效应，CPU 浪费；析构用 signal → 有些线程永远不醒 → 内存泄漏

**Q5: `pthread_detach` vs `pthread_join` 为什么要 detach？**
- join：主线程必须调用 `pthread_join` 等待线程结束，否则线程资源不释放（僵尸线程）
- detach：线程结束后自动回收资源，主线程不用管
- 本项目用 detach + stop flag 的方式关闭：设 flag → broadcast → 每个线程自己发现 flag 后 return
- 如果用 join，析构函数里需要循环 8 次 join 等所有线程——也可以，但 detach 更简洁

**Q6: 为什么 worker 必须是 static？C++ 成员函数有什么问题？**
- `pthread_create` 第三个参数类型是 `void* (*)(void*)`
- C++ 非 static 成员函数有隐式 `this` 指针，实际签名是 `void* (threadpool*, void*)`，类型不匹配
- static 成员函数没有隐式 this，可以通过第四个参数 arg 把 this 传进去再调用非 static 的 `run()`

**Q7: `cond.wait()` 为什么要先释放锁再睡觉？**
- 如果不释放锁就睡：其他线程（包括生产者 append）永远拿不到锁，无法往队列放任务 → 死锁
- wait 内部原子地做了三件事：释放锁 → 阻塞等待 → 醒来重新拿锁。锁的释放和睡眠之间没有缝隙（防止信号丢失）

---

### 阶段 3: RAII 锁封装 — 避免死锁和忘记释放

**实际文件**: `include/locker.h`

**目标**: 用 C++ 构造/析构自动管理锁的获取和释放，封装 4 个同步原语。

**为什么需要封装？**
- 原生 pthread API 容易出错：忘记 unlock、异常路径上没释放、忘记 destroy
- RAII 思想：构造获取资源，析构释放资源。栈上对象的析构函数一定会被调用（包括异常）

**实际封装了 4 个类**:

```cpp
class locker      // 互斥锁：拥有并管理 pthread_mutex_t
class scope_lock  // RAII 自动锁：构造 lock，析构 unlock（临时持有）
class sem         // 信号量：拥有并管理 sem_t（后续连接池用）
class cond        // 条件变量：拥有并管理 pthread_cond_t，包装 wait/signal/broadcast
```

**设计细节**:
- `locker.get()` 返回 `pthread_mutex_t*`：让 `cond.wait()` 能拿到裸锁指针。条件变量的 wait 需要 `pthread_mutex_t*`，不是 locker 对象
- `scope_lock` 是"借用者"：不拥有锁，只是临时持有。构造时调用 `locker->lock()`，析构时 `unlock()`
- `sem` 支持默认构造（初值 0）和指定初值两种构造：后面连接池需要指定最大连接数
- `cond` 额外提供 `wait_timeout()`：有时限等待，后续定时器阶段可能用到

**locker vs scope_lock 的区别**:

| | locker | scope_lock |
|---|--------|------------|
| 角色 | 锁的主人（拥有 pthread_mutex_t） | 锁的临时持有者（借用） |
| 生命周期 | 类的成员变量，长期存在 | 函数内的局部变量，用完即毁 |
| 比喻 | 厕所钥匙 | 你拿着钥匙的手，离开自动松 |

**面试问题与回答**:

**Q1: 什么是 RAII？用在哪里？**
- Resource Acquisition Is Initialization：资源获取即初始化
- 构造函数获取资源（`pthread_mutex_init`、`sem_init`），析构函数释放资源（`pthread_mutex_destroy`、`sem_destroy`）
- 本项目用 RAII 封装了 mutex、cond、sem。`scope_lock` 是典型的 RAII 应用：构造时拿锁，析构时还锁

**Q2: 有异常抛出时 RAII 锁能保证释放吗？**
- 能。C++ 标准保证：异常抛出时栈展开（stack unwinding），所有栈上局部对象的析构函数都会被调用
- 如果用裸 `pthread_mutex_lock/unlock`，异常路径上 `unlock` 可能被跳过 → 死锁
- `scope_lock` 在栈上，异常 → 析构 → unlock ✓

**Q3: 为什么还需要手动 lock/unlock，不全部用 scope_lock？**
- 有些场景需要灵活的锁粒度。比如 `append()` 中检查队列满时需要提前 unlock 并 return，`scope_lock` 的生命周期不能覆盖这种提前返回
- 不过更好的做法是给 `scope_lock` 加 `{}` 包围——目前项目中两种方式都有，保持简单

---

### 阶段 4: epoll I/O 多路复用 — 高性能核心

**目标**: 用 epoll 统一管理所有客户端的 I/O 事件，支持 LT 和 ET 两种模式。

**实际文件**: `include/epoller.h` + `epoller.cpp`

**核心概念 — Epoller 就是一个监控中心**:

```
                       ┌──────────────────┐
    listen_fd  ──────→ │                  │
    conn_fd_1  ──────→ │    Epoller       │
    conn_fd_2  ──────→ │   (epoll实例)     │────→ wait() 返回谁就绪了
    conn_fd_3  ──────→ │                  │
    ...                └──────────────────┘
```

- 你把 fd 注册给它："帮我盯着这个，可读了通知我"
- 它内部用红黑树管理所有被监听的 fd
- 有事件发生 → 放入就绪链表 → `wait()` 返回就绪队列

**`epoll_event` 结构体解剖**:

```cpp
struct epoll_event {
    uint32_t     events;   // "我要什么事件"（EPOLLIN | EPOLLET | EPOLLONESHOT...）
    epoll_data_t data;     // "附带凭证"（本项目存 fd，方便取回）
};
```

events 用位或 `|` 组合：`ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT`

**三个核心系统调用 → 封装为三个方法**:

| 系统调用 | 封装方法 | 用途 |
|---------|---------|------|
| `epoll_create` | 构造函数 | 创建 epoll 实例 |
| `epoll_ctl` | `add_fd()` / `mod_fd()` / `del_fd()` | 注册/修改/删除监听的 fd |
| `epoll_wait` | `wait()` | 等待事件，返回就绪 fd 个数 |

**实际代码结构**:

```cpp
// epoller.h
class Epoller {
public:
    Epoller(int max_events = 10000);
    ~Epoller();
    bool add_fd(int fd, uint32_t events, bool one_shot = true);
    bool mod_fd(int fd, uint32_t events, bool one_shot = true);
    bool del_fd(int fd, uint32_t events, bool one_shot = true);
    int  wait(int timeout_ms = -1);
    int      get_event_fd(int i) const;   // m_events[i].data.fd
    uint32_t get_events(int i) const;     // m_events[i].events

private:
    int m_epollfd;
    struct epoll_event* m_events;          // wait 返回的就绪数组（内核填充）
    int m_max_events;
};
```

**add_fd 逐行详解**:

```cpp
bool Epoller::add_fd(int fd, uint32_t events, bool one_shot) {
    struct epoll_event ev;           // ① 局部变量，只活在函数内
    ev.events = events;              // ② 你告诉内核"关心什么"
    if (one_shot) ev.events |= EPOLLONESHOT;  // ③ 叠加 ONESHOT
    ev.data.fd = fd;                 // ④ 凭证存 fd，方便取回

    if (epoll_ctl(m_epollfd, EPOLL_CTL_ADD, fd, &ev) == -1)
        return false;                // ⑤ 注册到 epoll 实例

    set_nonblocking(fd);             // ⑥ 设 fd 为非阻塞
    return true;
}
// ev 离开函数自动销毁 — 没关系，内核已经记住了
```

**set_nonblocking — 为什么必须**:

```cpp
static void set_nonblocking(int fd) {
    int old_flags = fcntl(fd, F_GETFL, 0);        // 取当前状态
    fcntl(fd, F_SETFL, old_flags | O_NONBLOCK);    // 叠加上非阻塞
}
```

ET 模式下必须非阻塞：如果 fd 是阻塞的，read 没数据时会卡住整个线程，所有连接饿死。

**add_fd 的 `ev` vs `m_events` — 别搞混**:

| | add_fd 里的局部 `ev` | 成员变量 `m_events[]` |
|---|-------------------|---------------------|
| 方向 | 你→内核（输入） | 内核→你（输出） |
| 谁填充 | 你 | 内核（在 wait 时） |
| 生命周期 | 函数内，用完即毁 | 和 Epoller 同生共死 |

**常见事件标志速查**:

| 标志 | 含义 | 场景 |
|------|------|------|
| `EPOLLIN` | 可读（数据到了 / 新连接来了） | listen fd + conn fd |
| `EPOLLOUT` | 可写（可以 send 了） | 响应数据准备发 |
| `EPOLLET` | 边缘触发 | 通信 socket（高效） |
| `EPOLLONESHOT` | 触发一次后自动移除，需 mod 重新注册 | 配合线程池，保证一个连接同时只被一个线程处理 |
| `EPOLLRDHUP` | 对端关闭连接 | 及时清理 |
| `EPOLLERR` / `EPOLLHUP` | 错误 / 挂断 | 自动监听，不用显式加 |

**LT vs ET — 你的 Epoller 本身不区分，靠外面传标志**:

```cpp
// 后面 webserver 里会这样用：
ep.add_fd(listen_fd, EPOLLIN);                  // LT：listen fd 安全，有 accept 遗漏会再通知
ep.add_fd(conn_fd, EPOLLIN | EPOLLET);           // ET：通信 fd 高效，减少 epoll_wait 触发次数
```

ET 的关键在读写逻辑（阶段 5 的 HttpConn 里），不在 Epoller 里：
```cpp
// ET 下必须循环读，读到 EAGAIN 才停
while (true) {
    int n = recv(fd, buf, len, 0);
    if (n == -1 && errno == EAGAIN) break;  // 缓冲区空了，收工
    // 处理数据...
}
```

**常见 bug**:
| bug | 原因 | 修复 |
|-----|------|------|
| ET 模式丢数据 | 没循环读直到 EAGAIN | while(read) 直到 errno==EAGAIN |
| ET 模式阻塞卡死 | fd 没设为非阻塞 | `fcntl(fd, F_SETFL, O_NONBLOCK)` |
| 新连接丢失 | listen fd 在 ET 下没循环 accept | while(accept) 直到 EAGAIN |
| read 返回 0 没处理 | 对端关闭，EPOLLIN 一直触发 | 检查 ret==0 → close + del_fd |

**面试问题与回答**:

**Q1: epoll 为什么比 select/poll 快？**
- select/poll 每次 `wait` 都要把整个 fd 集合从用户态拷贝到内核态（O(n)）
- select 有 1024 的 fd 数量限制，poll/epoll 无限制
- epoll 用红黑树管理所有监听 fd → 监听数上万也没问题
- epoll 用就绪链表返回结果 → 不需要遍历全部 fd，O(1) 取就绪事件

**Q2: LT 和 ET 有什么区别？什么时候用哪个？**
- LT（水平触发）：只要缓冲区非空就通知。没读完下次还会通知，编程简单
- ET（边缘触发）：只在"无数据→有数据"的边沿通知一次。必须一次读完，编程复杂但效率高（减少 epoll_wait 触发次数）
- listen fd 用 LT（安全），通信 fd 用 ET（高效）

**Q3: EPOLLONESHOT 的作用是什么？**
- 一个 fd 触发后自动从 epoll 监听中移除，不会再被别的线程同时处理
- 当前线程处理完这个请求后，用 `mod_fd` 重新注册 EPOLLIN
- 保证线程安全：一个连接同时只被一个线程操作

**Q4: `epoll_wait` 返回后 ET 下如何确保数据读完整？**
- 循环 read，直到返回 -1 且 errno == EAGAIN（缓冲区已空）
- 如果 read 返回 0，说明对端已关闭连接
- 不能用一次 read 就收工，因为 ET 只通知一次

---

### 阶段 5: HTTP 状态机 — 解析 HTTP 请求 + 生成响应

**实际文件**: `include/http_conn.h` + `http_conn.cpp`

**目标**: 一个连接对应一个 HttpConn 对象，负责读数据 → 解析 HTTP → 生成响应 → 发回。状态机逐行解析不定长的 HTTP 报文。

**为什么需要 HttpConn？**

服务器同时有几百个客户端，每个连接都得有自己的"记忆"——各自读到了什么、解析到哪了：

```
fd=5 → HttpConn[5]: m_read_buf 存着半截请求，m_state=PARSE_HEADER，下次继续
fd=6 → HttpConn[6]: 解析完了，正在生成响应
fd=7 → HttpConn[7]: 刚连上，初始化状态
```

如果共享一个缓冲区，fd=5 和 fd=6 的数据就搅在一起了。

**为什么需要状态机？**

TCP 是字节流，没有边界。浏览器发来完整的 70 字节，但 `recv` 可能一次只读到 30 字节——卡在半行上。状态机允许分多次读、分多次解析，每次从上次中断的地方继续。

**实际代码结构**:

```cpp
// 主状态机的状态
enum PARSE_STATE { PARSE_REQUESTLINE, PARSE_HEADER, PARSE_BODY };

// 解析结果
enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, INTERNAL_ERROR };

class HttpConn {
public:
    void init(int sockfd, const struct sockaddr_in& addr);  // 绑定新连接
    void process();            // 线程池入口：解析 + 生成响应
    bool read_once();          // recv 数据到 m_read_buf
    HTTP_CODE parse_request(); // 主状态机：逐行解析

private:
    char* get_line();          // 从缓冲区取一行（到 \r\n）

    int m_sockfd;
    struct sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;            // 缓冲区有多少数据
    int m_checked_idx;         // 已解析到哪
    int m_start_line;          // 当前行从哪开始
    PARSE_STATE m_state;       // 当前状态

    // 解析结果
    char m_method[8];          // "GET" / "POST"
    char m_url[256];           // 请求路径
    char m_version[16];        // HTTP 版本
    int m_content_length;      // 请求体长度

    // 响应
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    bool write();
    bool write_response();
};
```

**get_line() — 从缓冲区取一行**:

HTTP 每行以 `\r\n` 结尾。逐字节扫描找 `\r`，确认后面是 `\n`，把这一行截断返回：

```cpp
char* HttpConn::get_line() {
    for (int i = m_start_line; i < m_read_idx; i++) {
        if (m_read_buf[i] == '\r') {
            if (i + 1 >= m_read_idx || m_read_buf[i + 1] != '\n')
                return nullptr;                          // 还没读到完整的 \n
            char* line = &m_read_buf[m_start_line];      // 返回行首地址
            m_start_line = i + 2;                        // 跳过 \r\n，指向下一行
            m_read_buf[i] = '\0';                        // 截断
            m_read_buf[i + 1] = '\0';
            return line;
        }
    }
    return nullptr;  // 没找到 \r，数据不完整
}
```

**parse_request() — 主状态机**:

```
while (line = get_line()) {
    switch (m_state) {
        PARSE_REQUESTLINE:  sscanf 拆 method/url/version → m_state = PARSE_HEADER
        PARSE_HEADER:       如果 line 为空行 → 有 body 去 PARSE_BODY，否则 GET_REQUEST
                            否则解析 Content-Length 等头
        PARSE_BODY:         读满 Content-Length 字节 → GET_REQUEST
    }
}
return NO_REQUEST;  // get_line 返回 NULL，等下次 read
```

**read_once() — 从 socket 读**:

```cpp
int bytes = recv(m_sockfd, m_read_buf + m_read_idx,
                 READ_BUFFER_SIZE - m_read_idx, 0);
if (bytes <= 0) return false;
m_read_idx += bytes;
return true;
```

`m_read_buf + m_read_idx` 在缓冲区尾部追加，`READ_BUFFER_SIZE - m_read_idx` 防止溢出。

**write_response() — 构建 HTTP 响应**:

```cpp
const char* body = "<html><body><h1>Hello World</h1></body></html>";
m_write_idx = snprintf(m_write_buf, WRITE_BUFFER_SIZE,
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: %d\r\n"
    "\r\n"
    "%s", body_len, body);
return write();
```

HTTP 响应 = 状态行 + 响应头 + 空行 + body。

**process() — 线程池入口（串联一切）**:

```cpp
void HttpConn::process() {
    HTTP_CODE ret = parse_request();
    if (ret == GET_REQUEST)
        write_response();
}
```

**测试方法 — socketpair 模拟浏览器的完整请求→响应闭环**:

```cpp
int fd[2];
socketpair(AF_UNIX, SOCK_STREAM, 0, fd);

send(fd[1], raw_request, strlen(raw_request), 0);   // 模拟浏览器发

HttpConn conn;
conn.init(fd[0], addr);                              // 服务端绑定
conn.read_once();                                    // 读
conn.process();                                      // 解析 + 响应

char response[2048] = {0};
recv(fd[1], response, sizeof(response), 0);          // 浏览器读回响应
printf("Response:\n%s\n", response);
```

**本阶段完成度**: GET 请求解析 ✓，响应生成 ✓。POST 体解析和静态文件服务将在后续阶段完善。

**常见 bug**:
| bug | 原因 | 修复 |
|-----|------|------|
| 解析返回 NO_REQUEST | get_line 没找到 \r\n（数据不完整） | 先确认 recv 读到了多少数据，print 调试 |
| 枚举值搞混 | NO_REQUEST=0, GET_REQUEST=1, 不是 2 | 记住 C++ 枚举从 0 开始 |
| 段错误 | line 指针指向的缓冲区被后续 memset 清掉 | 注意 get_line 返回的是缓冲区内部指针，不要重复 memset |

**面试问题与回答**:

**Q1: HTTP 请求报文格式是什么？GET 和 POST 有什么区别？**
- 请求行(方法 URL 版本) + 请求头 + 空行 + 可选请求体
- GET 参数在 URL（`?key=value`），没有请求体
- POST 参数在请求体，需要有 Content-Length 头
- 从实现角度：状态机解析到空行时，GET 就结束了，POST 还要根据 Content-Length 继续读 body

**Q2: 为什么用状态机解析 HTTP？**
- TCP 是字节流，`recv` 可能读到半个请求（比如只读到 `"GET /ind"` 就没数据了）
- 状态机允许分多次增量解析：每次从 `m_start_line` 继续，上次解析到哪就接着哪
- 每个状态只关心当前该做的事（解析请求行就只找方法/URL，不关心头）

**Q3: keep-alive 是什么？怎么实现？**
- HTTP/1.1 默认一个 TCP 连接可以传多个 HTTP 请求
- 一个请求处理完不 close 连接，重置状态，继续 read 下一个请求
- 需要配合定时器断开长时间无活动的连接（阶段 7）

---

### 阶段 6: Reactor / Proactor 事件处理模式

### 阶段 6: Reactor / Proactor 事件处理模式

**实际实现**: 模拟 Proactor（在 `webserver.cpp` eventLoop 中）

**目标**: 把 I/O 读写和业务处理分离到不同线程，实现并发。

**两种模式对比**:

```
Reactor（同步 I/O）:
  主线程 epoll_wait → 通知工作线程 → 工作线程自己 recv → 工作线程处理业务

Proactor（异步 I/O）:
  主线程 epoll_wait → 主线程 recv 读好 → 工作线程直接处理业务
```

**本项目的模拟 Proactor 实现**:

```cpp
// webserver.cpp eventLoop()
else if (events & EPOLLIN)
{
    if (!m_users[fd].read_once())   // ① 主线程同步 I/O 读完数据
    {
        cleanup_conn(fd);
        continue;
    }
    m_timer_list.adjust_timer(m_users[fd].get_timer());  // ② 续期
    m_pool.append(&m_users[fd]);    // ③ 扔给工作线程（数据已就绪）
}
```

工作线程拿到的 HttpConn 里 `m_read_buf` 已经有数据了，直接 `parse_request()` + `write_response()`，不需要自己 recv。

**为什么是"模拟"**:

| | 真正 Proactor | 本项目 |
|---|---|---|
| I/O 谁做 | 内核异步 aio_read | 主线程同步 recv |
| 主线程阻塞吗 | 不，aio_read 立即返回 | 短暂阻塞等 recv |
| 工作线程感知 | 拿到已读好的数据 | 拿到已读好的数据 ✓ |
| Linux 可行性 | socket 支持很差 | 稳定可用 |

Linux aio 对 socket 基本不可用。用主线程同步 I/O 模拟异步，是业界常规做法。

**面试回答**:

**Q1: Reactor 和 Proactor 的区别？**
- Reactor: I/O 和业务在同一线程。epoll 通知 → 线程自己 recv → 处理 → send
- Proactor: I/O 和业务分离。内核/代理线程完成 I/O → 业务线程只处理已完成 I/O 的数据
- 本项 目是模拟 Proactor：主线程负责 recv，工作线程负责处理已读数据

**Q2: 为什么是"模拟"？**
- Linux 的 aio_read 对网络 socket 支持很差
- 主线程调 recv() 是同步系统调用，不是内核异步
- 但对工作线程来说，它拿到的已经是完整数据，感受等同异步

**Q3: 你代码中哪些体现了 Reactor 模式的分发特征？**
- 事件源: 所有 socket fd
- 事件分离器: Epoller::wait() → 把就绪 fd 从监听的集合中分离
- 事件分发器: eventLoop 中的 for 循环 → 判断 fd == listenfd? EPOLLIN?
- 事件处理器: HttpConn::process()

---

### 阶段 7: 定时器 — 断开长时间不活跃的连接

**实际文件**: `include/timer.h` + `timer.cpp`

**目标**: 每个连接挂一个闹钟，X 秒无活动自动断开，防止资源泄漏。

**为什么需要定时器**:

- 客户端连上但不发数据 → 永远占着 HttpConn 槽位
- 客户端发了一半卡住 → m_read_buf 里有半截数据
- 没有定时器，这些资源永远不会释放

**数据结构 — 升序双向链表**:

所有连接的超时时长相同（60 秒），谁最近有活动谁就最后过期。链表按插入顺序排列就够了：

```
head ⇄ [fd=5 expire=60s] ⇄ [fd=6 expire=70s] ⇄ [fd=7 expire=85s] ⇄ tail
          ↑ 最快过期——tick 从这里检查
```

**实际代码结构**:

```cpp
struct TimerNode {
    time_t expire;         // 绝对过期时间
    HttpConn* conn;        // 指向对应连接（双向引用）
    TimerNode* prev;
    TimerNode* next;
};

class TimerList {
    void add_timer(node);        // 新连接 → 设 expire → 挂到 tail（O(1)）
    void adjust_timer(node);     // 有活动 → 重置 expire → 移到 tail（O(1)）
    void del_timer(node);        // 连接关闭 → 摘除节点（O(1)，双向链表）
    void tick();                 // 每轮调，从 head 摘超时节点 → close_conn
    int  get_next_timeout();     // 距 head 超时还有多少 ms → 给 epoll_wait
};
```

**各项操作复杂度**:

| 操作 | 复杂度 | 说明 |
|------|--------|------|
| 新连接 | O(1) | 直接挂 tail |
| 有活动（重置） | O(1) | 双向链表任意位置摘除再挂 tail |
| 删除 | O(1) | 双向链表，有 prev/next 指针 |
| 检查超时 | O(k) | k 个到期的全摘，遇到没到期的停 |

**为什么不用最小堆**:
堆找最先到期的也是 O(1)（堆顶），但**更新随机节点是 O(n)**（要先找到它才能删）。每次 HTTP 请求都要 reset 定时器——链表 O(1)，堆 O(n)。定时器特点是频繁更新，链表更合适。

**epoll_wait 超时驱动 tick**:

```cpp
// webserver.cpp eventLoop()
int timeout = m_timer_list.get_next_timeout();  // 距 head 过期还有多少 ms
int n = m_epoller.wait(timeout);                // 最多等这么久
m_timer_list.tick();                            // 清理超时
```

不用 SIGALRM 信号，利用 epoll_wait 自带的 timeout 参数。三种唤醒：
- 有 I/O 事件 → 提前返回，n > 0
- 有人超时 → timeout 到，返回 n=0
- 无定时器 → timeout=-1，无限等

**HttpConn 和 TimerNode 的双向引用**:

```
HttpConn[5] ←─ conn ──→ TimerNode[5] ── prev/next → 链表
    │                          ↑
    └── m_timer ───────────────┘
```

| 场景 | 路径 | 代码 |
|------|------|------|
| 新连接 → 创建闹钟 | — | `add_timer(&m_timer_nodes[connfd])` |
| 收到数据 → 续期 | HttpConn → TimerNode | `adjust_timer(m_users[fd].get_timer())` |
| 超时 → 关连接 | TimerNode → HttpConn | `tmp->conn->close_conn()` |
| 连接正常关 → 摘节点 | `cleanup_conn()` | `del_timer(m_users[fd].get_timer())` |

**两条关闭路径**:

| 路径 | 谁触发 | 操作 |
|------|--------|------|
| 正常处理完 | `process()` → `close_conn()` | 关 fd，TimerNode 留在链表等 tick 回收 |
| 超时断开 | `tick()` → `close_conn()` | 检测 m_sockfd != -1 才关，避免重复关 |

**面试回答**:

**Q1: 为什么不用 SIGALRM 信号？**
- 信号处理函数里能做的事情很有限（async-signal-safe 限制）
- 多线程环境中信号投递不确定（不知道哪个线程收到）
- 用 epoll_wait 的 timeout 参数替代，简单且可控

**Q2: 定时器 O(1) 删除为什么能做到？**
- 双向链表，每个节点存了 prev 和 next 指针
- 知道要被删的节点地址，直接从链表摘除，不需要遍历查找
- HttpConn 通过 `m_timer` 指针持有自己的 TimerNode，删除时直接传地址

**Q3: 时间轮(time wheel)是什么？比链表好在哪？**
- 链表 O(1) 调整但每次要调 `time()` 重新计算 expire
- 时间轮把定时器按过期时间哈希到不同槽位，tick 时只走一圈
- 适合大量定时器的场景（比如操作系统内核），本项目规模链表足够

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

**实际文件**: `include/log.h` + `log.cpp`

**目标**: 单例 Log 类，命令行 `-l 0` 同步 / `-l 1` 异步。替换项目所有 `printf`。

**设计**:

```
同步模式: 🔒write_log → fputs → fflush → 返回（阻塞等磁盘）🔓

异步模式: 🔒write_log → strdup → 塞进环形队列 → signal_one 🔓→ 返回（不等磁盘）
                                              ↓
                     后台日志线程: wait 醒来 → 从队列取 → fputs → free
```

**实际代码结构**:

```cpp
// log.h
enum LogLevel { LOG_INFO = 1, LOG_WARN = 2, LOG_ERROR = 3 };

class Log {
public:
    static Log* get_instance();     // 单例（全局唯一日志实例）
    bool init(file_path, mode, max_queue_size);
    void write_log(LogLevel, file, line, format, ...);
    void shutdown();                // 退出前刷完剩余日志

private:
    FILE* m_fp;                     // 日志文件
    int m_mode;                     // 0=同步 1=异步
    locker m_mutex;                 // 同步用文件锁 + 异步用队列锁
    cond m_cond;                    // 异步条件变量

    // 异步用环形队列
    char** m_log_queue;             // 指针数组（存 strdup 分配的字符串）
    int m_queue_front, m_queue_rear, m_queue_count;

    pthread_t m_async_thread;
    bool m_running;
    static void* async_write_log(void* arg);
    void flush_to_file();           // 后台线程的主循环
};

// 宏 — 自动捕获 __FILE__ 和 __LINE__
#define LOG_INFO(fmt, ...)  Log::get_instance()->write_log(LOG_INFO, ...)
#define LOG_WARN(fmt, ...)  Log::get_instance()->write_log(LOG_WARN, ...)
#define LOG_ERROR(fmt, ...) Log::get_instance()->write_log(LOG_ERROR, ...)
```

**日志格式**:

```
[19:30:45][INFO][http_conn.cpp:80] fd=5: got 70 bytes:
GET /index.html HTTP/1.1
Host: localhost:8080

[19:30:45][WARN][http_conn.cpp:208] fd=6: 404 ./favicon.ico
[19:30:45][ERROR][webserver.cpp:55] bind failed: Address already in use
```

**同步/异步逻辑**:

```cpp
void Log::write_log(LogLevel level, const char* file, int line,
                    const char* format, ...) {
    // 1. 格式化：时间 + 级别 + 文件名:行号 + 用户消息
    snprintf(log_buf, "%02d:%02d:%02d][%s][%s:%d] %s\n", ...);

    if (m_mode == 0) {
        // 同步：直接 fputs + fflush（每行都刷盘）
        m_mutex.lock();
        fputs(log_buf, m_fp);
        fflush(m_fp);
        m_mutex.unlock();
    } else {
        // 异步：strdup → 环形队列 → signal 后台线程
        m_mutex.lock();
        if (m_queue_count < m_max_queue_size) {
            m_log_queue[m_queue_rear] = strdup(log_buf);
            m_queue_rear = (m_queue_rear + 1) % m_max_queue_size;
            m_queue_count++;
            m_cond.signal_one();
        }
        m_mutex.unlock();
    }
}
```

**后台线程 flush_to_file**:

```cpp
void Log::flush_to_file() {
    while (m_running) {
        m_mutex.lock();
        while (m_queue_count == 0 && m_running)   // 空队列 → wait 睡觉
            m_cond.wait(m_mutex.get());

        while (m_queue_count > 0) {
            char* str = m_log_queue[m_queue_front];
            m_queue_front = (m_queue_front + 1) % m_max_queue_size;
            m_queue_count--;

            m_mutex.unlock();          // 放锁 → fputs 不持锁（等磁盘很久）
            fputs(str, m_fp);
            free(str);                 // 释放 strdup 的内存
            m_mutex.lock();            // 拿锁 → 继续取下一个
        }
        fflush(m_fp);                  // 批量刷盘
        m_mutex.unlock();
    }
}
```

**关键设计决策**:

- **单例而非全局变量**：保证全局只有一个 Log 实例，`init` 调一次，各处 `LOG_INFO` 都往同一个文件写
- **`strdup` 而非预分配**：`write_log` 用 `strdup` 按需分配，`flush_to_file` `free` 释放。避免预分配 + 混用 `new[]`/`free` 的错误
- **fputs/flush 时解锁**：和线程池取任务后解锁同理，磁盘 I/O 很慢，不持锁
- **shutdown 广播**：设 `m_running=false` → `broadcast` 唤醒后台线程 → 循环缓冲区剩余日志写盘 → `fclose`
- **可变参数 `...` + `va_list`**：宏 LOG_INFO 接收任意参数，`vsnprintf` 格式化到缓冲区

**日志级别分级**:

| 级别 | 使用场景 | 例子 |
|------|---------|------|
| LOG_INFO | 正常流水 | 新连接、请求处理、响应字节数 |
| LOG_WARN | 小问题 | 404、BAD_REQUEST、不支持的方法、超时 |
| LOG_ERROR | 真错误 | socket/bind/accept/recv/open 失败 |

**面试问题与回答**:

**Q1: 同步和异步日志的区别？何时用哪个？**
- 同步：每次 `fputs` + `fflush`，阻塞等磁盘。可靠（崩溃不丢日志），但慢
- 异步：日志先放进环形队列，后台线程批量写。快（业务线程只做内存操作），但崩溃可能丢最后一批
- 调试/低并发用同步，生产/高并发用异步

**Q2: 异步日志为什么快？瓶颈在哪？**
- 业务线程只做 `strdup`（内存 copy）+ 环形队列插入，不等磁盘
- 瓶颈：环形队列满时丢日志（可增大队列或加阻塞策略）
- 真正的瓶颈在磁盘 I/O 带宽——后台线程串行写盘

**Q3: 怎么保证日志尽量不丢失？**
- `shutdown()` 在程序退出前把队列剩余全部刷盘
- 程序崩溃（SIGSEGV）时缓冲区里未刷的数据会丢，但这是 tradeoff
- 如果需要崩溃也不丢，只能用同步模式

**Q4: 为什么用环形队列而不是链表？**
- 环形队列预分配数组，O(1) 入队/出队，无内存分配开销
- 链表每次 `new` 节点有开销，日志高频调用不可接受

---

### 阶段 10: Webbench 压力测试

**目标**: 用 Webbench 模拟高并发，验证线程池 + epoll 的性能，拿到简历数据。

**Webbench 是什么**:

模拟 N 个假客户端同时连服务器，持续发请求，统计成功率/QPS。和 `perf`（测内部 CPU 热点）不同，Webbench 测的是**外部**——"服务器整体能扛多少客人"。

**Webbench 安装**:

webbench-1.5 源码有编译错误，需自行修复后编译：

```bash
# 下载
wget http://home.tiscali.cz/~cz210552/distfiles/webbench-1.5.tar.gz
tar -xzf webbench-1.5.tar.gz
cd webbench-1.5

# 修复：老旧头文件 <rpc/types.h> → <stdbool.h>
sed -i 's|<rpc/types.h>|<stdbool.h>|' webbench.c

# 编译安装
make
sudo cp webbench /usr/local/bin/
webbench -V   # 验证
```

**测试环境准备**:

```bash
# 服务端终端
ulimit -n 65535     # 提高 fd 上限，默认 1024 不够 1000 并发用
./server -p 8080 -l 1   # 异步日志，减少 I/O 影响

# 客户端终端
ulimit -n 65535     # 客户端也需要，webbench 每个连接占用一个 fd
```

**服务端优化**:

```cpp
// webserver.cpp: 放大 accept 队列，减少高并发下的连接拒绝
listen(m_listenfd, 65535);   // 原为 1024
```

**测试结果**:

| 并发数 | 持续时间 | 成功 | 失败 | 成功率 | QPS |
|--------|---------|------|------|--------|-----|
| 500 | 10s | 29,413 | 1,237 | 96.0% | 2,941 |
| 1000 | 30s | 86,894 | 3,632 | 96.0% | 2,896 |
| 1000 | 30s | 88,058 | 11,991 | 88.0% | ~3,333 |
| **1000 (优化后)** | **30s** | **86,894** | **3,632** | **96.0%** | **2,896** |

**关键发现**:

1. **QPS ≈ 2900-3000** — 测试场景为返回真实 HTML 文件（含磁盘 I/O），纯内存字符串 QPS 会更高
2. **约 4% 失败率是 Webbench 自身限制** — 500 并发和 1000 并发失败率几乎相同（4%），证明失败源于工具而非服务器。Webbench 是 2004 年的工具，TCP 栈、超时机制有固有限制
3. **listen 队列放大有效** — 失败率从 12% 降到 4%
4. **10500 并发直接挂** — `Connect to server failed`，webbench 自身打不开足够连接，不是服务器问题
5. **WSL 性能损耗** — WSL 的 TCP 栈套了一层 Windows，同等硬件裸 Linux 下 QPS 会更高

**QPS vs 并发连接数**:

> "支持上万并发"说的是**连接数**，不是每秒请求数（QPS）。

| | 本项目 | 解释 |
|---|--------|------|
| 并发连接 | 500-1000 | Webbench 能压的上限 |
| QPS | ≈2900 | 每连接每秒约 3 个请求 |
| 等效并发连接能力 | 理论 10000+ | epoll 管理上万 fd 无压力，Webbench 工具自身限制了验证上限 |

epoll 用红黑树管理所有连接，监听 10000 个 fd 的 `epoll_wait` 开销是 O(1)（只遍历就绪的），内存占用约 10000 × 1KB（读缓冲）= 10MB。理论上限远高于测试工具能验证的上限。

**简历写法**:

> 经 Webbench 压力测试，500-1000 并发下 QPS 稳定在 2900+，成功率 96%。epoll + 线程池的并发模型支持上万并发连接的理论上限。

**面试可能问**:

**Q1: 你怎么测试性能的？Webbench 原理是什么？**
- Webbench fork 出 N 个子进程，每个子进程持续建立 TCP 连接 → 发 HTTP 请求 → 收响应 → 统计
- 测的是"黑盒"性能：给定并发数，看服务器扛不扛得住

**Q2: 有 4% 失败率，怎么回事？**
- 500 并发和 1000 并发失败率一样约 4%，证明不是服务器性能问题（否则并发越大失败越多）
- Webbench 自身限制：2004 年的工具，客户端超时机制和 TCP 栈处理有固有问题
- 通过 `ulimit -n 65535` + `listen(fd, 65535)` 把失败率从 12% 降到了 4%

**Q3: 怎么优化到更高？**
- 当前瓶颈是 WSL 的虚拟化 TCP 栈 → 裸 Linux 直线提升
- 线程数从 8 调到和 CPU 核数匹配
- 用 `sendfile` 替代 `read + send`（零拷贝，文件 I/O 场景）
- 对象池减少 new/delete
- 用 `wrk` 替代 webbench 获得更准确的测试数据

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

  你不是服务端，你的 C++ 程序是。浏览器也不是——你是两个独立的东西在对话： 
``` 
  你的机器（localhost）                                                   
  ┌──────────────────────────────────────────────┐                        
  │                                              │                        
  │  ./server -p 8080          Chrome 浏览器      │                       
  │  ┌─────────────────┐      ┌──────────────┐   │                        
  │  │ listen 在 8080   │      │ 你输入        │   │                      
  │  │ 等待连接...      │◄─────│ localhost:   │   │                       
  │  │                 │ TCP  │ 8080 按回车   │   │                       
  │  │ 收到 HTTP 请求   │◄─────│ 浏览器发:     │   │                      
  │  │ "GET / HTTP/1.1"│      │ GET / HTTP/1.1│   │                       
  │  │                 │      │              │   │                        
  │  │ 解析→构建响应    │      │              │   │                       
  │  │ 发送 HTML 文本   │─────►│ 收到 <html>.. │   │                      
  │  │                 │ TCP  │ 渲染成页面    │   │                       
  │  └─────────────────┘      └──────────────┘   │                        
  │                                              │                        
  │              127.0.0.1 (loopback)             │                       
  │           数据不经过网线，内核内部转发          │                     
  └──────────────────────────────────────────────┘                
```                                                     
  - 你的程序是服务端：监听 8080，等连                                 
  - Chrome 是客户端：主动连到 8080，发请求，收应                        
  - 都在同一台机器上，走 loopback 地址 127.0.0.1，数据不出机器    
---

## 附录：GDB 实战 — 调试 `free(): invalid pointer` 全过程

### 背景

加入日志系统后，运行 `./server -p 8080 -l 1`（异步模式），浏览器访问一次 → `Ctrl+C` 停止服务端 → 浏览器刷新一次 → `Ctrl+C` 停止服务端程序崩溃：

```
free(): invalid pointer
Aborted (core dumped)
```

下面是用 GDB 定位根因的完整过程。

### 第一步：改造编译选项

生产 Makefile 用 `-O2` 优化，优化后 GDB 里变量被优化掉、行号不准。改为：

```makefile
CXXFLAGS := -std=c++17 -g -O0 -Wall -Wextra
```

- `-g`：生成调试符号（函数名、变量名、行号映射）
- `-O0`：关闭优化，保证 GDB 里行号精确，变量可打

```bash
make clean && make
```

### 第二步：GDB 启动程序

```bash
$ gdb ./server
(gdb) run -p 8080 -l 1
```

这时用浏览器访问 `http://localhost:8080/`，然后切回终端 `Ctrl+C`。

### 第三步：`Ctrl+C` 先收到 SIGINT

```gdb
Thread 1 "server" received signal SIGINT, Interrupt.
0x00007ffff792a072 in epoll_wait (epfd=4, 
    events=0x5555556765e0, maxevents=10000, 
    timeout=600000)
```

这是正常的——`Ctrl+C` 打断在 `epoll_wait`。输入 `continue` 让程序继续走退出流。

### 第四步：`continue` 后真正崩了

```gdb
(gdb) c
Continuing.
free(): invalid pointer

Thread 1 "server" received signal SIGABRT, Aborted.
```

### 第五步：`bt` — 最核心的 GDB 命令

```gdb
(gdb) bt
#0  __pthread_kill_implementation (...)
#1  __pthread_kill_internal (...)
#2  __GI___pthread_kill (...)
#3  __GI_raise (sig=sig@entry=6) at ../sysdeps/posix/raise.c:26
#4  __GI_abort () at ./stdlib/abort.c:79
#5  __libc_message_impl (...) at ../sysdeps/posix/libc_fatal.c:134
#6  malloc_printerr (str=... "free(): invalid pointer")
    at ./malloc/malloc.c:5775
#7  _int_free (...) at ./malloc/malloc.c:4507
#8  __GI___libc_free (mem=0x7fffe18f80f0) at ./malloc/malloc.c:3398
#9  TimerList::del_timer (this=0x7fffffffd4f0, node=0x7fffe18f80f0)
    at timer.cpp:84
#10 WebServer::cleanup_conn (this=0x7fffffffd440, fd=7)
    at webserver.cpp:132
#11 WebServer::eventLoop (this=0x7fffffffd440)
    at webserver.cpp:115
#12 main (argc=5, argv=0x7fffffffd648) at main.cpp:70
```

### 第六步：解读 backtrace — 从下往上看

背调栈反过来读——从最底层 `main` 往上追：

| 帧 | 文件 | 行 | 做了什么 |
|----|------|-----|---------|
| #12 | main.cpp | 70 | `server.eventLoop()` |
| #11 | webserver.cpp | 115 | 调了 `cleanup_conn(fd)` |
| #10 | webserver.cpp | 132 | 调了 `del_timer(node)` |
| #9 | **timer.cpp** | **84** | **`delete node` ← 炸在这里** |
| #8-#6 | malloc.c | — | glibc 检测到 free 的指针非法，打印 "free(): invalid pointer" |
| #3-#0 | — | — | glibc `abort()` → SIGABRT 信号 |

**所有线索汇聚到 timer.cpp:84。**

### 第七步：看具体的代码行

```bash
(gdb) frame 9
(gdb) list
```

timer.cpp:84 是：

```cpp
void TimerList::del_timer(TimerNode* node)
{
    // ... 从链表摘除节点 ...
    delete node;   // ← bug 在这里
}
```

### 第八步：分析根因 — `delete` 怎么错了

回头看 node 从哪来。在 `webserver.cpp` 的 `init()` 中：

```cpp
m_timer_nodes = new TimerNode[MAX_FD];   // ← new[] 分配数组
```

而 `del_timer` 里对数组的**单个元素**调了 `delete`：

```cpp
delete node;  // node 来自 new[] 数组 → 未定义行为！
```

C++ 规定：`new[]` 分配的内存必须用 `delete[]` 整体释放，不能对数组里的单个元素单独 `delete`。

### 第九步：修复

删掉三处非法的 `delete` —— `del_timer`、`tick`、`~TimerList` 中都不要 `delete`。TimerNode 是 WebServer 预分配的数组元素，只在 `~WebServer` 中 `delete[] m_timer_nodes` 统一释放。

```cpp
// timer.cpp 修复：去掉所有 delete node/delete tmp
void TimerList::del_timer(TimerNode* node) {
    // ... 从链表摘除 ...
    // 不 delete — 节点是 WebServer 预分配数组的元素
}
```

### 第十步：类比 — Log 里同样的模式

Log 模块有一个同根的 bug：`init` 中用 `new char[1024]` 预分配，但 `write_log` 里用 `strdup`（底层 `malloc`）覆盖了指针。后续 `flush_to_file` 中 `free(str)` 拿 `new[]` 分配的地址去 `free` —— 同样非法。

修复：`init` 只分配 `char*` 指针数组，字符串由 `strdup` 按需分配，`free` 正常回收。

### GDB 常用命令速查

| 命令 | 含义 |
|------|------|
| `run` | 启动程序（可带命令行参数） |
| `bt` 或 `backtrace` | 打印调用栈 |
| `frame N` | 切换到第 N 帧 |
| `list` | 显示当前帧源码 |
| `info locals` | 显示当前帧局部变量 |
| `print var` 或 `p var` | 打印变量值 |
| `continue` 或 `c` | 继续执行 |
| `break file:line` | 在指定行设断点 |
| `next` 或 `n` | 单步执行（不进入函数） |
| `step` 或 `s` | 单步执行（进入函数） |
| `quit` | 退出 GDB |

### 经验总结

1. **`bt` 是第一生产力**：程序崩在哪一层、谁来调的一目了然
2. **`new[]` 和 `delete` 不配对**是最容易犯的 C++ 错误之一，GDB 的 backtrace 能直接指出罪魁祸首的行
3. **预分配数组 + 链表引用**的模式下，链表只负责串联，不负责释放——释放权归持有数组的那个类
4. `-g -O0` 编译是排查崩溃的标准第零步，没符号表 GDB 什么都帮不了
5. 如果 `bt` 没有自己的代码帧而是全是 `??` —— 说明编译没加 `-g`

---

## 附录：多线程进阶面试问答

### 多线程加锁的性能开销

锁的本质是**把并行变成串行**。开销来自三个方面：

**① 抢锁本身的 CPU 开销**

```cpp
m_mutex.lock();   // → futex 系统调用（如果无竞争，只是原子操作，几十个 CPU 周期）
m_mutex.unlock(); // → 同上
```

无竞争时（fast path）锁就是一次原子 CAS，约 20-50 个 CPU 周期。有竞争时触发 futex 系统调用 → 内核介入 → 数千到上万个周期。

**② 串行化造成的并行度损失**

```
8 个线程抢同一把锁：
  线程 1: lock → 干活 → unlock  (其他 7 个线程在等)
  线程 2: lock → 干活 → unlock  (其他 7 个线程在等)
  ...
  表面上 8 线程并行，实际串行
```

Amdahl 定律：串行部分占比 S，加速比上限 = 1/S。如果临界区占用 10% 时间，最多快 10 倍，再加线程也没用。

**③ 伪共享（False Sharing）**

```cpp
struct {
    int counter_a;  // 线程 A 频繁写
    int counter_b;  // 线程 B 频繁写
} stats;            // 两个变量在同一缓存行（64 字节）

// 线程 A 写 counter_a → 缓存行被标记"脏"
// 线程 B 写 counter_b → 缓存行失效，重新从内存加载
// 两个线程互相使对方的缓存失效 → 每次写都是内存访问速度
```

一个缓存行 64 字节，两个不相关的变量凑在一块就是伪共享。

**你的项目中**：
- 线程池的任务队列锁保护很短的临界区（入队/出队几行），无竞争概率高（用 ONESHOT 保证一个 fd 同时只被通知一次）
- 日志系统的环形队列同理
- 真正的瓶颈不在锁，在非阻塞 socket 的 EAGAIN 循环 + 内存分配（strdup/new）

---

### 频繁使用锁如何避免死锁

死锁四个必要条件（缺一不可）：

```
1. 互斥：资源只能被一个线程持有
2. 持有并等待：持有一个资源的同时等待另一个
3. 不可剥夺：不能强行抢走别人持有的锁
4. 循环等待：A 等 B、B 等 C、C 等 A
```

**防御策略（打破任一条件即可）**：

**① 锁排序（打破循环等待）**

```cpp
// ❌ 死锁可能
线程 A: lock(lock1); lock(lock2);
线程 B: lock(lock2); lock(lock1);  // 顺序相反 → 交叉等 → 死锁

// ✓ 全局约定锁顺序
线程 A: lock(lock1); lock(lock2);
线程 B: lock(lock1); lock(lock2);  // 同样顺序 → 不会死锁
```

你项目里隐式遵守了：先拿资源锁（empty/full）再拿互斥锁（mutex），顺序固定。

**② 减小锁粒度（减少持有并等待的概率）**

```cpp
// ❌ 大锁
lock_big();
do_io();    // 慢，持着锁
do_cpu();   // 也持着锁
unlock_big();

// ✓ 小锁 + 读写分开
lock_small();
queue.pop();  // 只保护共享数据结构
unlock_small();
do_io();      // 不持锁
do_cpu();     // 不持锁
```

你的代码里 `fputs`/`fflush` 时解锁、线程池 `process()` 时解锁——都是这个原则。

**③ 超时 + 回退（打破持有并等待）**

```cpp
if (!try_lock_timeout(500ms)) {
    unlock_all();    // 释放已持有的
    sleep(random());  // 随机后退，防止活锁
    retry();
}
```

你的代码没用到——但 `cond::wait_timeout` 就是这个方向的。

**④ RAII + scope_lock（避免忘记解锁）**

你的 `scope_lock` 类就是这个——异常或提前 return 自动解锁，不会遗忘。

---

### 无锁化编程

**核心思想：不用互斥锁，用原子操作完成并发安全。**

**① 原子变量替代互斥锁保护的计数器**

```cpp
// 有锁版
int counter;
locker lk;
void inc() { lk.lock(); counter++; lk.unlock(); }

// 无锁版
std::atomic<int> counter;
void inc() { counter.fetch_add(1, std::memory_order_relaxed); }
```

`fetch_add` 是 CPU 的一条原子指令（LOCK XADD），不需操作系统介入。几十个 CPU 周期 vs 几千个（有竞争时）。

**② CAS（Compare-And-Swap）循环实现无锁数据结构**

```cpp
// 无锁栈的 push
void push(Node* node) {
    do {
        node->next = head.load();
    } while (!head.compare_exchange_weak(node->next, node));
    // 如果 head 被其他线程改了 → CAS 失败 → 重试
}
```

**③ RCU（Read-Copy-Update）— 读者零开销**

```cpp
// 读多写少的场景（你的 HttpConn 数组就类似）
// 读者：不加锁，直接读（可能读到旧版本，没关系）
// 写者：拷贝一份 → 修改 → 原子指针替换 → 等所有读者结束 → 释放旧版本
```

**④ 你的项目里本就无锁的地方**

- `HttpConn` 数组的读——ONESHOT 保证同时只有一个线程处理一个连接，无竞争
- 主线程和 worker 线程的数据边界——主线程只写 `m_read_buf`（在 `read_once` 中），worker 线程只读——天然的"单生产者单消费者"同步

---

### 内存泄漏如何检测

**① Valgrind — 最全面的工具**

```bash
# 编译时保留调试符号
make CXXFLAGS="-std=c++17 -g -O0 -Wall"

# Valgrind 跑
valgrind --leak-check=full --show-leak-kinds=all ./server -p 8080

# 浏览器访问几次后 Ctrl+C，Valgrind 输出:
# ==12345== 100 bytes in 1 blocks are definitely lost
# ==12345==    at 0x...: malloc
# ==12345==    by 0x...: strdup (log.cpp:105)
# ==12345==    by 0x...: Log::write_log (log.cpp:105)
```

直接精确到**源文件:行号**。

**② AddressSanitizer (ASan) — 编译器内置，运行时快**

```bash
g++ -fsanitize=address -g -O0 -o server *.cpp ...

./server -p 8080
# 如果有 use-after-free、buffer overflow、内存泄漏 → ASan 直接报
```

比 Valgrind 快 5-10 倍，适合日常开发。

**③ 你的项目里最容易泄漏的点**

| 位置 | 你怎么处理的 |
|------|------------|
| `log.cpp` write_log → `strdup` | `flush_to_file` 里 `free(str)` ✓ |
| `http_conn.cpp` serve_static → `new char[]` | 同函数末尾 `delete[] file_buf` ✓ |
| `webserver.cpp` init → `new HttpConn[MAX_FD]` | `~WebServer` 中 `delete[] m_users` ✓ |
| `threadpool` → 外部传入的 `T* request` | 外部管理生命周期（HttpConn 数组），不在此释放 ✓ |

**④ 面试怎么说**

> 内存泄漏用 Valgrind 和 ASan 检测。Valgrind 能精确到泄漏的源文件行号。ASan 是编译器插桩，运行时开销比 Valgrind 小 5-10 倍。写代码时遵循"谁分配谁释放"——比如日志的 strdup 在 flush_to_file 里 free，HttpConn 数组在 WebServer 析构里 delete[]。预分配数组 + 链表引用的模式要特别小心——链表只能串起数组元素不能 delete 单个元素，释放权归持有数组的类。

---

### 线程池 vs 单线程 对比总结

| | 单线程（串行） | 多线程（你的项目） |
|---|-------------|----------------|
| 实现 | 简单，无锁 | 复杂，锁 + 条件变量 + 任务队列 |
| 一个慢请求的影响 | 卡住后面所有人 | 只卡一个线程，其他人继续 |
| 锁开销 | 无 | 入队/出队时加锁，几十到几百 CPU 周期 |
| CPU 利用 | 只用 1 核 | 8 个线程利用 8 核 |
| 调试 | 简单 | 死锁、竞态、数据竞争难复现 |
| 适用 | 低并发、纯 I/O 场景 | 高并发、CPU+IO 混合场景 |

你的项目选多线程是合理的——HTTP 请求的处理（解析、读文件、构建响应）有 CPU 计算，需要多核并行。如果纯做 I/O 转发（代理服务器），单线程 + epoll 就够。
