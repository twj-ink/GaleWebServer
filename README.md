# GaleWebServer

Linux 下的轻量级 C++ Web 服务器。

1. **并发模型**: 使用 **线程池 + 非阻塞socket + epoll(ET和LT均实现) + 模拟Proactor事件处理** 的并发模型
2. **HTTP**: 使用 **有限状态机** 解析HTTP请求报文，支持解析 **GET** 请求，可以请求服务器 **静态文件**
3. **日志系统**: 实现 **同步/异步日志系统** ，记录服务器运行状态和错误信息
4. **定时器**: 采用双向链表维护每个连接的可活动时长，**epoll_wait 超时驱动，O(1) 调整**
5. **压力测试**: 经过Webbench压力测试可以实现 **3000QPS 的吞吐量**

## 架构

```
main()
  ├── Cmdline:              命令行参数（端口、epoll模式、日志模式）
  └── WebServer:
        ├── Epoller:        epoll 实例，监控所有 fd，ET/LT可选择
        ├── threadpool:     8 个工作线程（模拟 Proactor）
        ├── HttpConn[]:     预分配数组，下标 = fd
        ├── TimerList:      双向链表，管理连接超时
        ├── Log:            单例，同步/异步可选择
        └── eventLoop():
              epoll_wait            返回就绪 fd
              → accept / read_once （模拟Proactor，主线程代理 I/O）
              → pool.append        （交给工作线程）
              → process()           解析 HTTP + 返回响应
```

## 性能

测试环境: WSL2 / 8 线程 / Webbench 1.5。4% 失败率为 Webbench 自身限制。裸 Linux 下 QPS 更高。

在上千并发的情况下，QPS大致稳定在2880，且有 4% 的失败率。


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
