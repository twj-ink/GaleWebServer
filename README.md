# GaleWebServer — 轻量级 C++ Web 服务器

Linux 下基于 epoll + 线程池的高并发 Web 服务器，支持静态文件服务、HTTP GET/POST 解析、数据库连接池、同步/异步日志。

## 技术栈

- **并发模型**: 线程池 + 非阻塞 socket + epoll (LT/ET) + 模拟 Proactor
- **HTTP**: 有限状态机解析 GET/POST 请求
- **数据库**: MySQL 连接池实现用户注册、登录
- **文件服务**: 静态资源（HTML / 图片 / 视频）
- **日志**: 同步 / 异步可切换，环形队列 + 后台线程批量写
- **定时器**: 升序双向链表，epoll_wait 超时驱动，O(1) 调整

## 架构

```
main()
  ├── Cmdline:      命令行参数（端口、日志模式）
  └── WebServer:
        ├── Epoller:        epoll 实例，监控所有 fd
        ├── threadpool:     8 个工作线程（模拟 Proactor）
        ├── HttpConn[]:     预分配数组，下标 = fd
        ├── TimerList:      双向链表，管理连接超时
        ├── Log:            单例，同步/异步可切换
        └── eventLoop():
              epoll_wait 返回就绪 fd
              → accept / read_once（主线程代理 I/O）
              → pool.append（交给工作线程）
              → process() 解析 HTTP + 返回响应
```

## 性能

| 并发 | 持续时间 | 成功请求 | QPS | 测试页面 |
|------|---------|---------|-----|---------|
| 500  | 10s     | 29,413  | 2,941 | index.html |
| 1000 | 30s     | 86,894  | 2,896 | index.html |

> 测试环境: WSL2 / 8 线程 / Webbench 1.5。4% 失败率为 Webbench 自身限制。裸 Linux 下 QPS 更高。

## 构建 & 运行

```bash
# 编译
make

# 运行（同步日志）
./server -p 8080 -l 0

# 运行（异步日志，高并发推荐）
./server -p 8080 -l 1

# 关闭日志
./server -p 8080 -c 1
```

浏览器访问 `http://localhost:8080/`，看到页面即启动成功。

## 项目结构

```
webserver-cpp/
├── main.cpp               # 入口
├── webserver.cpp           # 服务器主逻辑（eventLoop + accept）
├── http_conn.cpp           # HTTP 状态机 + 静态文件 + 响应
├── epoller.cpp             # epoll 封装（wait / add / mod / del）
├── timer.cpp               # 定时器（升序双向链表 + tick）
├── log.cpp                 # 日志系统（同步/异步 + 环形队列）
├── cmdline.cpp             # 命令行解析
├── include/
│   ├── webserver.h
│   ├── threadpool.h        # 线程池模板（pthread + 条件变量）
│   ├── http_conn.h
│   ├── epoller.h
│   ├── timer.h
│   ├── locker.h            # RAII 锁封装（locker / cond / sem / scope_lock）
│   ├── log.h
│   └── cmdline.h
├── Makefile
├── index.html              # 测试页面
├── test.jpg                # 测试图片
└── PROJECT_GUIDE.md        # 完整开发指南
```

## 核心设计要点

### 模拟 Proactor

```
Reactor:  主线程通知工作线程 → 工作线程自己 recv → 处理
Proactor: 内核异步 I/O → 回调
本项目:   主线程 recv 读好数据 → 交给工作线程处理（模拟异步）
```

- 主线程负责 epoll_wait + read_once（I/O 代理）
- 工作线程只做业务处理（HTTP 解析 + 响应）
- Linux AIO 对 socket 支持差，用同步 I/O 模拟异步是业界常规做法

### HTTP 状态机

```
PARSE_REQUESTLINE → PARSE_HEADER → PARSE_BODY → GET_REQUEST
```

TCP 字节流不能保证一次 recv 收到完整请求。状态机允许分多次增量解析，每次从上次中断处继续。

### 定时器

- 升序双向链表，所有连接超时时长相同（60s）
- 最近活跃的连接在尾部，最久未活跃的在头部
- 新连接 O(1) 挂尾，活跃连接 O(1) 移到尾
- `epoll_wait(timeout)` 驱动 tick，不用信号

### 日志系统

```
同步: 业务线程 → fputs → fflush（等磁盘）
异步: 业务线程 → strdup → 环形队列 → signal → 返回（不等磁盘）
                  后台线程: wait 醒来 → 批量写盘 → free
```

- 单例保证全局唯一日志实例
- 宏自动捕获 `__FILE__` 和 `__LINE__`
- 三级日志: INFO（流水）/ WARN（404等）/ ERROR（系统调用失败）
- 环形队列满时静默丢弃 + shutdown 时统计丢失数

## 压力测试

```bash
# 安装 Webbench
wget http://home.tiscali.cz/~cz210552/distfiles/webbench-1.5.tar.gz
tar -xzf webbench-1.5.tar.gz && cd webbench-1.5
sed -i 's|<rpc/types.h>|<stdbool.h>|' webbench.c  # 修复编译
make && sudo cp webbench /usr/local/bin/

# 测试
ulimit -n 65535
webbench -c 1000 -t 30 http://localhost:8080/
```

## 许可

MIT License
