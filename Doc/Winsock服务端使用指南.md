# Winsock TCP 服务端使用指南

> 适用项目：VirtualBenth `device_simulator`
> 文档目标：系统说明 Winsock 2.2 TCP 服务端的常用类型、函数、返回值、错误处理和 I/O 模型。
> 项目对应：当前代码使用单线程阻塞 I/O；本文同时讲清 `select()`、非阻塞模式和其他常见方案，供后续扩展时查阅。

## 1. 为什么本项目选择 Winsock

`device_simulator/CMakeLists.txt` 已在 Windows 下链接 `ws2_32`，不需要新增第三方依赖。当前服务端只有一个客户端，阻塞的 `accept()` 和 `recv()` 已足够，也最适合学习以下基础问题：

- Socket 的创建、监听、连接和关闭顺序；
- TCP 是字节流，没有消息边界；
- `recv()` 可能返回半帧、一帧或多帧；
- `send()` 可能只发送一部分数据；
- 正常断开与错误返回的区别；
- 网络层、流式解析层和业务层的职责边界。

`protocol` 静态库继续保持纯 C++17，不包含任何 Winsock 头文件。所有平台相关代码只放在 `device_simulator` 内。

## 2. Winsock 服务端的完整生命周期

```text
WSAStartup
    ↓
socket 创建监听套接字
    ↓
bind 绑定本地地址与端口
    ↓
listen 进入监听状态
    ↓
accept 接受一个客户端
    ↓
recv → StreamParser::feed → 业务处理
    ↓                              ↓
客户端断开                     ProtocolCodec::encode
    ↓                              ↓
closesocket(client)             send_all
    ↓
closesocket(listener)
    ↓
WSACleanup
```

通用服务端通常会在外层循环中重复 `accept()`；当前项目代码是“单次 `accept()`、单客户端、连接结束后进程退出”。这是实现范围，不是 Winsock API 的限制。

## 3. 头文件和 CMake

### 3.1 头文件顺序

Winsock 2 必须优先于可能间接包含旧版 Winsock 的 Windows 头文件：

```cpp
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
```

不要先包含 `<windows.h>` 再包含 `<winsock2.h>`，否则容易发生类型或宏重复定义。

### 3.2 链接库

项目现有 CMake 已具备所需链接配置：

```cmake
if (WIN32)
    target_link_libraries(device_simulator PRIVATE ws2_32)
endif ()
```

因此源码中不需要使用 `#pragma comment(lib, "ws2_32.lib")` 再建立第二套链接规则。

## 4. 重要类型和返回值

| API / 类型 | 成功 | 失败或特殊情况 |
| --- | --- | --- |
| `WSAStartup()` | `0` | 非零错误码 |
| `socket()` | `SOCKET` | `INVALID_SOCKET` |
| `bind()` / `listen()` | `0` | `SOCKET_ERROR` |
| `accept()` | 客户端 `SOCKET` | `INVALID_SOCKET` |
| `recv()` | `> 0`：收到的字节数 | `0`：对端正常关闭；`SOCKET_ERROR`：错误 |
| `send()` | `> 0`：本次发送的字节数 | `SOCKET_ERROR`：错误 |
| `connect()` | `0` | `SOCKET_ERROR`；非阻塞连接常见 `WSAEWOULDBLOCK` |
| `select()` | `> 0`：就绪 Socket 总数 | `0`：超时；`SOCKET_ERROR`：错误 |
| `ioctlsocket()` | `0` | `SOCKET_ERROR` |
| `setsockopt()` / `getsockopt()` | `0` | `SOCKET_ERROR` |
| `getaddrinfo()` | `0` | 非零的名称解析错误码 |
| `WSAPoll()` | `> 0`：发生事件的元素数 | `0`：超时；`SOCKET_ERROR`：错误 |
| `WSAGetLastError()` | 最近一次 Winsock 错误码 | 只在 Winsock API 失败后读取 |

注意：

- `SOCKET` 不是可移植的普通文件描述符，不要存进 `int`。
- Windows 使用 `closesocket()`，不是 POSIX 的 `close()`。
- 判断错误应使用 `INVALID_SOCKET` 或 `SOCKET_ERROR`，不要自行假设其数值。

### 4.1 核心函数声明说明

下面的声明保留了 Winsock 2 的关键类型和调用约定，但省略了 Windows SDK 头文件中的 SAL 参数注解，便于阅读。编译时不要自己重复声明这些函数，应直接包含 `<winsock2.h>` 和 `<ws2tcpip.h>`。

### 4.2 `WSAStartup`：初始化 Winsock

```cpp
int WSAAPI WSAStartup(
    WORD version_required,
    LPWSADATA data);
```

参数：

- `version_required`：请求的 Winsock 版本，通常传 `MAKEWORD(2, 2)`；
- `data`：输出参数，接收实际提供的版本和实现信息。

返回值：

- `0`：成功；
- 非 `0`：错误码直接由返回值给出。

用法：

```cpp
WSADATA data{};
const int startup_error = ::WSAStartup(MAKEWORD(2, 2), &data);
if (startup_error != 0)
{
    // WSAStartup 的错误码直接使用 startup_error，
    // 不要依赖尚未成功初始化的 WSAGetLastError()。
}
```

初始化成功后还应检查 `data.wVersion` 是否确实为 2.2。一个进程可以调用多次 `WSAStartup()`，但每次成功调用都必须有对应的 `WSACleanup()`；本项目只需要在程序入口初始化一次。

### 4.3 `WSACleanup`：释放 Winsock 运行时

```cpp
int WSAAPI WSACleanup(void);
```

返回值：

- `0`：成功；
- `SOCKET_ERROR`：失败，可再读取 `WSAGetLastError()`。

用法：

```cpp
const int cleanup_result = ::WSACleanup();
if (cleanup_result == SOCKET_ERROR)
{
    const int error = ::WSAGetLastError();
}
```

所有 Socket 都应先关闭，再调用 `WSACleanup()`。实际项目中优先由 `WinsockRuntime` 析构函数自动配对，而不是在多个返回路径手动清理。

### 4.4 `socket`：创建 Socket

```cpp
SOCKET WSAAPI socket(
    int address_family,
    int socket_type,
    int protocol);
```

本项目的调用：

```cpp
const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
if (listener == INVALID_SOCKET)
{
    const int error = ::WSAGetLastError();
}
```

参数组合：

- `AF_INET`：IPv4；
- `SOCK_STREAM`：有序字节流；
- `IPPROTO_TCP`：TCP。

成功返回可用的 `SOCKET`，失败返回 `INVALID_SOCKET`。不要使用 `SOCKET_ERROR` 判断 `socket()` 的返回值。

### 4.5 `htons` 与 `inet_pton`：准备地址

```cpp
u_short WSAAPI htons(u_short host_value);

INT WSAAPI inet_pton(
    INT address_family,
    PCSTR text_address,
    PVOID binary_address);
```

`htons()` 把 16 位主机整数转换为网络字节序：

```cpp
sockaddr_in address{};
address.sin_family = AF_INET;
address.sin_port = ::htons(12345);
```

`inet_pton()` 把文本 IP 地址写入二进制地址结构：

```cpp
const int result = ::inet_pton(
    AF_INET,
    "127.0.0.1",
    &address.sin_addr);

if (result != 1)
{
    // 1 表示转换成功；0 表示文本地址格式无效；
    // SOCKET_ERROR 表示发生 Winsock 错误。
}
```

这里的 `htons()` 只用于 TCP/IP 端口，不能用于 VirtualBenth 帧内字段。

### 4.6 `bind`：绑定本地地址

```cpp
int WSAAPI bind(
    SOCKET socket,
    const sockaddr* address,
    int address_length);
```

用法：

```cpp
const int result = ::bind(
    listener,
    reinterpret_cast<const sockaddr*>(&address),
    static_cast<int>(sizeof(address)));

if (result == SOCKET_ERROR)
{
    const int error = ::WSAGetLastError();
}
```

- `socket`：尚未绑定的监听 Socket；
- `address`：指向具体地址结构，这里实际是 `sockaddr_in`；
- `address_length`：地址结构字节数。

成功返回 `0`，失败返回 `SOCKET_ERROR`。

### 4.7 `listen`：进入监听状态

```cpp
int WSAAPI listen(
    SOCKET socket,
    int backlog);
```

用法：

```cpp
const int result = ::listen(listener, SOMAXCONN);
if (result == SOCKET_ERROR)
{
    const int error = ::WSAGetLastError();
}
```

- `socket`：已经成功 `bind()` 的 TCP Socket；
- `backlog`：等待 `accept()` 的连接队列上限提示。

`listen()` 只把 Socket 变成监听 Socket，不会返回具体客户端连接。

### 4.8 `accept`：取得一个客户端连接

```cpp
SOCKET WSAAPI accept(
    SOCKET listening_socket,
    sockaddr* peer_address,
    int* peer_address_length);
```

需要对端地址时：

```cpp
sockaddr_in peer{};
int peer_length = static_cast<int>(sizeof(peer));

const SOCKET client = ::accept(
    listener,
    reinterpret_cast<sockaddr*>(&peer),
    &peer_length);

if (client == INVALID_SOCKET)
{
    const int error = ::WSAGetLastError();
}
```

不关心对端地址时可以写成：

```cpp
const SOCKET client = ::accept(listener, nullptr, nullptr);
```

阻塞模式下，若当前没有客户端，`accept()` 会等待。成功返回一个新的客户端 Socket；监听 Socket 仍然存在，可在当前会话结束后继续接受下一连接。

### 4.9 `recv`：从 TCP 字节流读取数据

```cpp
int WSAAPI recv(
    SOCKET socket,
    char* buffer,
    int buffer_length,
    int flags);
```

用法：

```cpp
std::array<std::uint8_t, 4096> buffer{};

const int received = ::recv(
    client,
    reinterpret_cast<char*>(buffer.data()),
    static_cast<int>(buffer.size()),
    0);

if (received > 0)
{
    // 只处理 [0, received) 范围，并交给 StreamParser。
}
else if (received == 0)
{
    // 对端正常关闭连接。
}
else // received == SOCKET_ERROR
{
    const int error = ::WSAGetLastError();
}
```

参数：

- `buffer`：接收目标，Winsock 接口使用 `char*`，因此字节数组需要显式转换；
- `buffer_length`：本次最多可写入的字节数，类型是 `int`；
- `flags`：当前项目传 `0`。

正返回值只是本次读取的字节数，不代表一帧。只能把 `[buffer.begin(), buffer.begin() + received)` 交给 `StreamParser::feed()`。

### 4.10 `send`：向 TCP 字节流提交数据

```cpp
int WSAAPI send(
    SOCKET socket,
    const char* buffer,
    int buffer_length,
    int flags);
```

单次调用示例：

```cpp
const int sent = ::send(
    client,
    reinterpret_cast<const char*>(bytes.data() + offset),
    request_size,
    0);

if (sent == SOCKET_ERROR)
{
    const int error = ::WSAGetLastError();
}
else
{
    offset += static_cast<std::size_t>(sent);
}
```

返回的正数是本次实际提交的字节数，可能小于 `buffer_length`。因此业务代码不应直接调用一次 `send()` 后就认为整帧完成，而应统一通过本文第 10 节的 `send_all()`。

### 4.11 `shutdown`：停止收发方向

```cpp
int WSAAPI shutdown(
    SOCKET socket,
    int how);
```

`how` 可取：

- `SD_RECEIVE`：停止接收；
- `SD_SEND`：停止发送；
- `SD_BOTH`：停止收发。

用法：

```cpp
if (::shutdown(client, SD_BOTH) == SOCKET_ERROR)
{
    const int error = ::WSAGetLastError();
}
```

`shutdown()` 改变连接的收发状态，但不会释放 Socket 句柄。之后仍必须调用 `closesocket()`。

### 4.12 `closesocket`：释放 Socket 句柄

```cpp
int WSAAPI closesocket(SOCKET socket);
```

用法：

```cpp
if (::closesocket(client) == SOCKET_ERROR)
{
    const int error = ::WSAGetLastError();
}
```

成功返回 `0`，失败返回 `SOCKET_ERROR`。同一个句柄只能关闭一次；使用 RAII 包装后，不要再在外部手动重复关闭。

### 4.13 `WSAGetLastError`：读取 Winsock 错误码

```cpp
int WSAAPI WSAGetLastError(void);
```

典型用法：

```cpp
if (::bind(listener, address_ptr, address_size) == SOCKET_ERROR)
{
    const int error = ::WSAGetLastError();
    log_socket_error("bind", error);
}
```

应在 Winsock API 明确失败后立即读取错误码，避免其他 API 调用覆盖线程的最近错误。例外是 `WSAStartup()`：它已经直接返回错误码。

### 4.14 核心调用顺序速查

```cpp
WSAStartup(MAKEWORD(2, 2), &data);

listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
bind(listener, address, address_size);
listen(listener, SOMAXCONN);

client = accept(listener, peer_address, peer_address_size);

while (connected)
{
    received = recv(client, buffer, buffer_size, 0);
    // received > 0: StreamParser::feed()
    // received == 0: 对端正常关闭
    // received == SOCKET_ERROR: WSAGetLastError()

    send(client, response, response_size, 0); // 实际代码使用 send_all()
}

shutdown(client, SD_BOTH);
closesocket(client);
closesocket(listener);
WSACleanup();
```

这段只展示 API 顺序。真实代码仍应使用 RAII、检查每个返回值，并用 `send_all()` 处理部分发送。

### 4.15 `getaddrinfo` / `freeaddrinfo`：解析主机、服务和地址族

相比已经过时的 `gethostbyname()`，`getaddrinfo()` 可以统一处理主机名、数字地址、IPv4 和 IPv6：

~~~cpp
int WSAAPI getaddrinfo(
    const char* node_name,
    const char* service_name,
    const addrinfo* hints,
    addrinfo** result);

void WSAAPI freeaddrinfo(addrinfo* result);
~~~

服务端解析本地监听地址的典型写法：

~~~cpp
addrinfo hints{};
hints.ai_family = AF_INET;       // 当前项目只使用 IPv4；通用程序可选 AF_UNSPEC
hints.ai_socktype = SOCK_STREAM;
hints.ai_protocol = IPPROTO_TCP;
hints.ai_flags = AI_PASSIVE;

addrinfo* result = nullptr;
const int error = ::getaddrinfo(
    nullptr,                     // AI_PASSIVE + nullptr：本地监听地址
    "12345",                     // 服务名也可以直接写十进制端口字符串
    &hints,
    &result);

if (error != 0)
{
    // getaddrinfo 直接返回自己的错误码，不使用 WSAGetLastError() 代替。
    throw std::runtime_error("getaddrinfo failed: " + std::to_string(error));
}

// result 是链表；应逐项尝试 socket()、bind()，不能假定第一项必定可用。
for (addrinfo* current = result; current != nullptr; current = current->ai_next)
{
    // current->ai_family
    // current->ai_socktype
    // current->ai_protocol
    // current->ai_addr
    // current->ai_addrlen
}

::freeaddrinfo(result);
result = nullptr;
~~~

关键点：

- `result` 是动态分配的链表，成功后必须用 `freeaddrinfo()` 释放；
- `AI_PASSIVE` 适合服务端生成可用于 `bind()` 的地址；
- 客户端解析远端地址时通常不设置 `AI_PASSIVE`；
- 当前项目直接构造 `sockaddr_in` 是有效的 IPv4 最小实现；需要 IPv6 或主机名时再改用 `getaddrinfo()`。

### 4.16 `connect`：客户端主动建立连接

虽然本文件主要讲服务端，未来 `host_gui` 客户端会使用 `connect()`：

~~~cpp
int WSAAPI connect(
    SOCKET socket,
    const sockaddr* remote_address,
    int address_length);
~~~

阻塞 Socket 中，`connect()` 成功返回 `0`，失败返回 `SOCKET_ERROR`。非阻塞 Socket 中，连接不能立即完成时通常返回 `SOCKET_ERROR`，`WSAGetLastError()` 为 `WSAEWOULDBLOCK`；这不代表最终失败，而是需要等待连接完成：

- 用 `select()`：成功完成通常出现在 `writefds`，失败出现在 `exceptfds`；
- 或用 `WSAEventSelect()` / `WSAAsyncSelect()` 等通知模型；
- 不要反复调用 `connect()` 来轮询完成状态。

### 4.17 `select`：同步等待多个 Socket 就绪

#### 4.17.1 函数声明

~~~cpp
int WSAAPI select(
    int nfds,
    fd_set* readfds,
    fd_set* writefds,
    fd_set* exceptfds,
    const timeval* timeout);
~~~

`select()` 不直接收发数据，只回答“哪些 Socket 现在执行某类操作不会立即阻塞”。它适合少量连接的单线程同步复用。

`select()` 可以监视阻塞 Socket，也可以监视非阻塞 Socket；调用 `select()` 本身不会改变 Socket 的阻塞模式。

参数含义：

| 参数 | Windows/Winsock 含义 |
| --- | --- |
| `nfds` | 在 Winsock 中被忽略，只为兼容 Berkeley sockets；通常传 `0` |
| `readfds` | 检查可读事件；监听 Socket 可读表示有连接待 `accept()` |
| `writefds` | 检查可写事件；也用于非阻塞 `connect()` 成功完成 |
| `exceptfds` | 主要用于带外数据，或非阻塞 `connect()` 失败；不是“所有网络错误集合” |
| `timeout` | `nullptr` 无限等待；正值限时等待；`{0, 0}` 立即轮询 |

三个集合至少有一个非空指针，且所有非空集合不能同时为空；同一次调用中的 Socket 还应来自兼容的服务提供者。

返回值：

- `> 0`：三个集合中就绪 Socket 的总数；
- `0`：等待超时，没有 Socket 就绪；
- `SOCKET_ERROR`：失败，立即读取 `WSAGetLastError()`。

#### 4.17.2 `fd_set` 与四个宏

~~~cpp
fd_set read_set;
FD_ZERO(&read_set);              // 清空集合
FD_SET(listener, &read_set);     // 添加 Socket
FD_CLR(client, &read_set);       // 删除 Socket

if (FD_ISSET(client, &read_set))
{
    // client 位于 select() 返回的就绪子集中。
}
~~~

Winsock 的 `fd_set` 不是 Unix 上按文件描述符数值建立的位图，不应自己访问或猜测其内部布局；常规增删检查使用 `FD_*` 宏。需要遍历 Winsock 返回集合时，可以读取 Windows 定义的 `fd_count` 和 `fd_array[]`。

默认 `FD_SETSIZE` 是 64，即一个 `fd_set` 默认最多保存 64 个 Socket。若确实要修改，必须在包含 `winsock2.h` 之前统一定义，并确保所有相关翻译单元一致。连接数明显增大时，不应仅靠扩大 `FD_SETSIZE`，而应评估 `WSAPoll`、重叠 I/O 或 IOCP。

#### 4.17.3 每次调用前都要重新准备集合

`readfds`、`writefds` 和 `exceptfds` 是输入/输出参数。`select()` 返回时，它们已经被改写为“本次就绪的子集”。因此不能把返回后的集合原样用于下一轮：

~~~cpp
fd_set master_read;
FD_ZERO(&master_read);
FD_SET(listener, &master_read);

while (running)
{
    fd_set ready_read = master_read; // 每轮复制一份给 select 修改
    timeval timeout{1, 0};           // 1 秒；每轮重新建立也更利于跨平台

    const int ready_count = ::select(
        0,                           // Winsock 忽略 nfds
        &ready_read,
        nullptr,
        nullptr,
        &timeout);

    if (ready_count == SOCKET_ERROR)
    {
        const int error = ::WSAGetLastError();
        break;
    }
    if (ready_count == 0)
    {
        // 超时：可处理定时任务、退出标志或统计。
        continue;
    }

    // 只遍历 ready_read 中的就绪 Socket。
}
~~~

Windows 的 `select()` 不会修改传入的 `timeval` 内容，但每轮重新构造可避免把平台差异带进未来的可移植代码。

#### 4.17.4 “可读”分别表示什么

监听 Socket 出现在 `readfds`：

- 至少有一个连接进入等待队列；
- 此时调用 `accept()` 通常可以立即取得客户端，而不会继续等待。

已连接 Socket 出现在 `readfds`：

- 有普通数据可读；
- 或对端已正常关闭；
- 或连接已被重置、终止。

因此，可读后仍必须真正调用 `recv()` 并检查三种结果：

~~~cpp
const int received = ::recv(client, buffer, buffer_size, 0);

if (received > 0)
{
    // 处理 [0, received)。
}
else if (received == 0)
{
    // 对端有序关闭。
}
else
{
    const int error = ::WSAGetLastError();
    // 非阻塞模式下 WSAEWOULDBLOCK 通常表示状态已被其他操作消耗，可稍后重试。
}
~~~

`select()` 报告“某一时刻的就绪状态”，不替代 `recv()`、`send()`、`accept()` 的返回值检查。

#### 4.17.5 不要把所有客户端永久放进 `writefds`

TCP Socket 在大多数时间都处于可写状态。如果每一轮都把所有连接放进 `writefds`，`select()` 很可能立即返回，事件循环会持续空转并占满 CPU。

正确策略：

- 只有某个连接存在“尚未发送完的应用层缓冲区”时，才把它加入 `writefds`；
- 可写后继续发送剩余部分；
- 待发送缓冲区清空后立即从写集合移除；
- 即使 `select()` 表示可写，一次 `send()` 仍可能只发送部分数据。

#### 4.17.6 一个监听 Socket + 多个客户端的核心循环

下面只演示 `select()` 的集合管理。真实代码还要为每个客户端维护独立的 `StreamParser`、会话状态和待发送缓冲：

~~~cpp
fd_set master_read;
FD_ZERO(&master_read);
FD_SET(listener, &master_read);

bool running = true;
while (running)
{
    fd_set ready_read = master_read;
    timeval timeout{1, 0};

    const int ready_count = ::select(
        0,
        &ready_read,
        nullptr,
        nullptr,
        &timeout);

    if (ready_count == SOCKET_ERROR)
    {
        log_socket_error("select", ::WSAGetLastError());
        break;
    }
    if (ready_count == 0)
    {
        continue;
    }

    // Winsock 的 fd_set 保存 fd_count 和 fd_array[]。
    for (u_int i = 0; i < ready_read.fd_count; ++i)
    {
        const SOCKET ready_socket = ready_read.fd_array[i];

        if (ready_socket == listener)
        {
            const SOCKET client = ::accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET)
            {
                log_socket_error("accept", ::WSAGetLastError());
                continue;
            }

            if (master_read.fd_count >= FD_SETSIZE)
            {
                // 当前 select 容量已满，不能静默漏管新连接。
                ::closesocket(client);
                continue;
            }

            FD_SET(client, &master_read);
            // 同时创建并登记 client 专属的 StreamParser 和 SessionState。
            continue;
        }

        std::array<char, 4096> buffer{};
        const int received = ::recv(
            ready_socket,
            buffer.data(),
            static_cast<int>(buffer.size()),
            0);

        if (received > 0)
        {
            // 把实际字节交给 ready_socket 专属的 StreamParser。
            continue;
        }

        if (received == SOCKET_ERROR)
        {
            const int error = ::WSAGetLastError();
            if (error == WSAEWOULDBLOCK)
            {
                continue;
            }
            log_socket_error("recv", error);
        }

        // received == 0 或不可恢复错误：先移出集合，再关闭并删除连接状态。
        FD_CLR(ready_socket, &master_read);
        ::closesocket(ready_socket);
    }
}
~~~

该循环解决的是“一个线程等待多个 Socket”。它没有自动解决：

- 每连接解析缓存和会话状态的所有权；
- 部分发送和发送背压；
- 慢客户端导致的待发送数据积压；
- 应用层帧接收超时；
- 线程安全、跨线程关闭与程序退出唤醒。

### 4.18 `ioctlsocket`：切换非阻塞模式和查询可读字节

~~~cpp
int WSAAPI ioctlsocket(
    SOCKET socket,
    long command,
    u_long* argument);
~~~

设置非阻塞模式：

~~~cpp
u_long nonblocking = 1;
if (::ioctlsocket(client, FIONBIO, &nonblocking) == SOCKET_ERROR)
{
    const int error = ::WSAGetLastError();
}
~~~

恢复阻塞模式：

~~~cpp
u_long blocking = 0;
::ioctlsocket(client, FIONBIO, &blocking);
~~~

非阻塞模式下：

- `accept()`、`recv()`、`send()`、`connect()` 暂时无法完成时通常返回 `SOCKET_ERROR`；
- `WSAGetLastError() == WSAEWOULDBLOCK` 表示“现在不能完成，稍后重试”，不是必须立即断开；
- 不能用无延迟死循环不断重试，应配合 `select()`、`WSAPoll()` 或其他通知机制。

`FIONREAD` 可查询当前可立即读取的字节数量，但它不是协议帧长度，也不能替代 `StreamParser`：

~~~cpp
u_long available = 0;
if (::ioctlsocket(client, FIONREAD, &available) == SOCKET_ERROR)
{
    const int error = ::WSAGetLastError();
}
~~~

### 4.19 `setsockopt` / `getsockopt`：设置和读取 Socket 选项

~~~cpp
int WSAAPI setsockopt(
    SOCKET socket,
    int level,
    int option_name,
    const char* option_value,
    int option_length);

int WSAAPI getsockopt(
    SOCKET socket,
    int level,
    int option_name,
    char* option_value,
    int* option_length);
~~~

常用选项：

| level / 选项 | 作用 | 使用提醒 |
| --- | --- | --- |
| `SOL_SOCKET / SO_EXCLUSIVEADDRUSE` | 阻止其他 Socket 强行绑定同一地址和端口 | Windows 服务端通常在 `bind()` 前设置 |
| `SOL_SOCKET / SO_REUSEADDR` | 允许强制复用地址 | Windows TCP 服务端不要照搬 Unix 习惯盲目开启，可能产生端口劫持或不确定分发 |
| `SOL_SOCKET / SO_RCVTIMEO` | 阻塞接收超时 | Windows 常用毫秒 `DWORD`；超时后 `recv()` 失败并给出 `WSAETIMEDOUT` |
| `SOL_SOCKET / SO_SNDTIMEO` | 阻塞发送超时 | 仍必须处理部分发送 |
| `SOL_SOCKET / SO_KEEPALIVE` | 开启 TCP keep-alive | 不是应用层 ACK 超时，也不能替代帧接收超时 |
| `IPPROTO_TCP / TCP_NODELAY` | 禁用 Nagle 算法 | 低延迟小包可能有用，但会增加包数；应基于测量决定 |
| `SOL_SOCKET / SO_ERROR` | 读取并清除 Socket 保存的错误 | 常用于确认非阻塞 `connect()` 结果 |

设置独占绑定：

~~~cpp
BOOL exclusive = TRUE;
if (::setsockopt(
        listener,
        SOL_SOCKET,
        SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusive),
        static_cast<int>(sizeof(exclusive))) == SOCKET_ERROR)
{
    const int error = ::WSAGetLastError();
}
~~~

设置 2 秒阻塞接收超时：

~~~cpp
DWORD timeout_ms = 2000;
if (::setsockopt(
        client,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout_ms),
        static_cast<int>(sizeof(timeout_ms))) == SOCKET_ERROR)
{
    const int error = ::WSAGetLastError();
}
~~~

读取每 Socket 错误：

~~~cpp
int socket_error = 0;
int option_length = static_cast<int>(sizeof(socket_error));

if (::getsockopt(
        client,
        SOL_SOCKET,
        SO_ERROR,
        reinterpret_cast<char*>(&socket_error),
        &option_length) == SOCKET_ERROR)
{
    const int api_error = ::WSAGetLastError();
}
~~~

`SO_ERROR` 是 Socket 级错误状态；`WSAGetLastError()` 是当前线程最近一次失败调用的错误码，两者不是同一概念。

### 4.20 `getsockname` / `getpeername` / `inet_ntop`：查看端点

| 函数 | 用途 |
| --- | --- |
| `getsockname()` | 读取本地绑定地址和端口；绑定端口 0 时可取得系统实际分配的端口 |
| `getpeername()` | 读取已连接 Socket 的对端地址和端口 |
| `inet_ntop()` | 把二进制 IPv4/IPv6 地址转换为可打印文本 |

示例：

~~~cpp
sockaddr_in peer{};
int peer_length = static_cast<int>(sizeof(peer));

if (::getpeername(
        client,
        reinterpret_cast<sockaddr*>(&peer),
        &peer_length) == 0)
{
    std::array<char, INET_ADDRSTRLEN> text{};
    if (::inet_ntop(
            AF_INET,
            &peer.sin_addr,
            text.data(),
            static_cast<DWORD>(text.size())) != nullptr)
    {
        const unsigned short port = ::ntohs(peer.sin_port);
    }
}
~~~

### 4.21 `WSAPoll`：数组形式的就绪等待

~~~cpp
int WSAAPI WSAPoll(
    WSAPOLLFD* descriptors,
    ULONG descriptor_count,
    INT timeout_ms);
~~~

`WSAPoll()` 与 POSIX `poll()` 的使用形态更接近：每个数组元素同时保存 Socket、关注事件和返回事件，不受 `fd_set` 的默认 64 项布局约束。

~~~cpp
std::vector<WSAPOLLFD> descriptors;
descriptors.push_back({listener, POLLRDNORM, 0});

const int result = ::WSAPoll(
    descriptors.data(),
    static_cast<ULONG>(descriptors.size()),
    1000);

if (result == SOCKET_ERROR)
{
    const int error = ::WSAGetLastError();
}
~~~

它仍是同步就绪轮询，不等于重叠 I/O 或 IOCP。选择 `select()` 还是 `WSAPoll()` 应根据目标 Windows 版本、连接规模、团队熟悉度和实测结果决定。

## 5. 用 RAII 管理 Winsock 生命周期

`WSAStartup()` 与 `WSACleanup()` 必须配对。可以使用一个不可复制的 RAII 类型：

```cpp
#include <stdexcept>

class WinsockRuntime
{
public:
    WinsockRuntime()
    {
        WSADATA data{};
        const int error = ::WSAStartup(MAKEWORD(2, 2), &data);
        if (error != 0)
        {
            throw std::runtime_error("WSAStartup failed: " + std::to_string(error));
        }

        if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2)
        {
            ::WSACleanup();
            throw std::runtime_error("Winsock 2.2 is unavailable");
        }
    }

    ~WinsockRuntime()
    {
        ::WSACleanup();
    }

    WinsockRuntime(const WinsockRuntime&) = delete;
    WinsockRuntime& operator=(const WinsockRuntime&) = delete;
};
```

还需要包含 `<string>` 才能使用 `std::to_string()`。

`WinsockRuntime` 应在监听和客户端套接字之前创建，并在所有套接字关闭之后析构。不要为每个连接重复调用 `WSAStartup()`。

## 6. 用 RAII 管理 SOCKET

下面是最小的独占句柄包装。它负责关闭，但不负责业务逻辑：

```cpp
class SocketHandle
{
public:
    SocketHandle() noexcept = default;

    explicit SocketHandle(SOCKET socket) noexcept
        : socket_(socket)
    {
    }

    ~SocketHandle()
    {
        reset();
    }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    SocketHandle(SocketHandle&& other) noexcept
        : socket_(other.release())
    {
    }

    SocketHandle& operator=(SocketHandle&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] SOCKET get() const noexcept
    {
        return socket_;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return socket_ != INVALID_SOCKET;
    }

    SOCKET release() noexcept
    {
        const SOCKET result = socket_;
        socket_ = INVALID_SOCKET;
        return result;
    }

    void reset(SOCKET replacement = INVALID_SOCKET) noexcept
    {
        if (valid())
        {
            ::closesocket(socket_);
        }
        socket_ = replacement;
    }

private:
    SOCKET socket_{INVALID_SOCKET};
};
```

这个包装可以防止提前 `return` 或异常路径遗漏 `closesocket()`。本项目不需要把它设计成通用网络框架。

## 7. 创建监听套接字

### 7.1 创建 TCP Socket

```cpp
SocketHandle listener{
    ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
};

if (!listener.valid())
{
    throw std::runtime_error(
        "socket failed: " + std::to_string(::WSAGetLastError()));
}
```

- `AF_INET`：IPv4；
- `SOCK_STREAM`：面向字节流；
- `IPPROTO_TCP`：TCP。

当前实现只使用 IPv4；需要 IPv6 时可改用 `getaddrinfo()` 统一生成候选地址。

### 7.2 绑定回环地址

协议文档没有冻结端口号。端口属于应用配置，不属于帧协议。本项目建议：

- 只绑定 `127.0.0.1`，避免尚未完成认证和资源保护时暴露到局域网；
- 端口集中定义在一处，后续再改为命令行参数；
- 不要把端口写进 `Doc/协议文档.md` 的帧格式。

```cpp
constexpr unsigned short kListenPort = 12345; // 当前项目默认值，后续可改为应用配置

sockaddr_in address{};
address.sin_family = AF_INET;
address.sin_port = ::htons(kListenPort);

const int address_result = ::inet_pton(
    AF_INET,
    "127.0.0.1",
    &address.sin_addr);

if (address_result != 1)
{
    throw std::runtime_error("inet_pton failed");
}

const int bind_result = ::bind(
    listener.get(),
    reinterpret_cast<const sockaddr*>(&address),
    static_cast<int>(sizeof(address)));

if (bind_result == SOCKET_ERROR)
{
    throw std::runtime_error(
        "bind failed: " + std::to_string(::WSAGetLastError()));
}
```

### 7.3 网络字节序与协议字节序不能混用

`sockaddr_in::sin_port` 属于 TCP/IP 地址结构，所以必须用 `htons()` 转成网络字节序。

VirtualBenth 帧中的 `payload_len`、CRC 和 payload 多字节字段由项目协议明确规定为小端，必须继续由 `ProtocolCodec` 处理，不能对这些帧字段调用 `htons()` 或 `htonl()`。

```text
TCP/IP 地址与端口：网络字节序（大端）
VirtualBenth 帧字段：协议规定的小端
```

### 7.4 监听

```cpp
if (::listen(listener.get(), SOMAXCONN) == SOCKET_ERROR)
{
    throw std::runtime_error(
        "listen failed: " + std::to_string(::WSAGetLastError()));
}
```

当前实现只处理一个客户端，但监听队列仍可使用 `SOMAXCONN`。这不等于应用会并发处理多个连接。

## 8. 接受客户端

```cpp
sockaddr_in peer_address{};
int peer_length = static_cast<int>(sizeof(peer_address));

SocketHandle client{
    ::accept(
        listener.get(),
        reinterpret_cast<sockaddr*>(&peer_address),
        &peer_length)
};

if (!client.valid())
{
    throw std::runtime_error(
        "accept failed: " + std::to_string(::WSAGetLastError()));
}
```

阻塞模式下，`accept()` 会等待客户端连接。这是当前实现的预期行为，不需要仅为避免阻塞立即创建线程。

## 9. 接收数据并接入 StreamParser

### 9.1 正确理解 recv

一次 `recv()` 可能得到：

- 一个帧头的前 1B；
- 一帧的一部分；
- 恰好一帧；
- 多个连续帧；
- 上一帧尾部与下一帧头部的组合。

因此不能写成“一次 `recv()` 调一次 `ProtocolCodec::decode()`”。正确流程是把每次收到的任意字节块交给该连接自己的 `StreamParser`。

```cpp
#include <array>
#include <cstdint>
#include <vector>

StreamParser parser;
std::array<std::uint8_t, 4096> receive_buffer{};

while (true)
{
    const int received = ::recv(
        client.get(),
        reinterpret_cast<char*>(receive_buffer.data()),
        static_cast<int>(receive_buffer.size()),
        0);

    if (received > 0)
    {
        const std::vector<std::uint8_t> chunk(
            receive_buffer.begin(),
            receive_buffer.begin() + received);

        const std::vector<Frame> frames = parser.feed(chunk);
        for (const Frame& frame : frames)
        {
            // 交给业务处理函数；不要在这里重新解析 CRC 或帧长度。
            handle_frame(client.get(), frame);
        }
        continue;
    }

    if (received == 0)
    {
        // 对端进行了有序关闭。
        break;
    }

    const int error = ::WSAGetLastError();
    // 当前阻塞模型记录错误并结束当前会话。
    log_socket_error("recv", error);
    break;
}

parser.reset();
```

说明：

- 接收缓冲区大小不需要等于最大帧长；`StreamParser` 会跨多次调用缓存半包。
- `received` 为正数时才可以构造对应范围。
- `recv() == 0` 不是错误，而是对端正常关闭连接。
- 发生不可恢复的接收错误时关闭当前连接；是否回到 `accept()` 由服务端外层生命周期决定，当前项目会结束进程。

## 10. 正确处理部分发送

### 10.1 send 并不保证一次发完

即使使用阻塞 Socket，`send()` 的正返回值也只表示本次接受了多少字节。必须持续发送剩余部分：

```cpp
#include <algorithm>
#include <climits>

bool send_all(SOCKET socket, const std::vector<std::uint8_t>& bytes)
{
    std::size_t offset = 0;

    while (offset < bytes.size())
    {
        const std::size_t remaining = bytes.size() - offset;
        const int request_size = static_cast<int>(
            std::min(remaining, static_cast<std::size_t>(INT_MAX)));

        const int sent = ::send(
            socket,
            reinterpret_cast<const char*>(bytes.data() + offset),
            request_size,
            0);

        if (sent == SOCKET_ERROR)
        {
            log_socket_error("send", ::WSAGetLastError());
            return false;
        }

        if (sent == 0)
        {
            // 无法继续推进；按连接失败处理，避免无限循环。
            return false;
        }

        offset += static_cast<std::size_t>(sent);
    }

    return true;
}
```

虽然当前协议单帧最大只有 4104B，循环仍然是必要的正确性约束。不要因为测试时经常一次发送完整，就把它当成 API 保证。

## 11. 从业务响应到完整 TCP 发送

业务层先构造 `Frame`，再交给 `ProtocolCodec::encode()`；网络层只负责发送编码结果。

推荐把“编码一帧并完整发送”集中到一个函数：

~~~cpp
bool send_frame(SOCKET socket, const Frame& frame)
{
    const EncodeResult encoded = ProtocolCodec::encode(frame);
    if (!encoded.ok())
    {
        return false;
    }

    return send_all(socket, encoded.bytes);
}
~~~

三条控制命令成功时都必须先 ACK，再发布命令完成后的 STATUS：

| 请求 | ACK | STATUS |
| --- | --- | --- |
| HELLO | `ACK(HELLO, OK)` | `STATUS(READY)` |
| START | `ACK(START, OK)` | `STATUS(RUNNING)` |
| STOP | `ACK(STOP, OK)` | `STATUS(IDLE)` |

### 11.1 HELLO 成功响应示例

协议要求成功后依次发送：

1. `ACK(HELLO, OK)`；
2. `STATUS(READY)`。

```cpp
Frame ack;
ack.command = Command::Ack;
ack.payload = {
    static_cast<std::uint8_t>(Command::Hello),
    0x00 // OK
};

Frame status;
status.command = Command::Status;
status.payload = {
    0x01 // READY
};

const EncodeResult ack_result = ProtocolCodec::encode(ack);
const EncodeResult status_result = ProtocolCodec::encode(status);

if (!ack_result.ok() || !status_result.ok())
{
    // 本地构帧失败属于内部错误，当前会话不能继续假装成功。
    return false;
}

if (!send_all(client_socket, ack_result.bytes))
{
    return false;
}

return send_all(client_socket, status_result.bytes);
```

不能只发送 ACK，也不能用 STATUS 替代 ACK；二者语义不同。

如果 ACK 已发送成功而 STATUS 发送失败，双方可能对状态转换产生不同认识，当前连接不应继续处理后续命令。

### 11.2 业务校验

`ProtocolCodec::decode()` 和 `StreamParser` 只保证帧结构与 CRC 正确。业务层仍要检查：

| 命令 | payload 校验 | 状态校验 |
| --- | --- | --- |
| HELLO | 必须为空 | 只允许 AwaitHello |
| START | 恰好 5B；sample_rate 和 channel_mask 都不能为 0 | 只允许 Ready 或 Idle |
| STOP | 必须为空 | 只允许 Running |
| DATA / STATUS / ACK / ERROR | host→device 方向不合法 | 返回 BAD_STATE |
| 未知命令 | 不能崩溃或进入未定义逻辑 | 返回 UNKNOWN_CMD |

版本不匹配时应发送 `ERROR(VER_MISMATCH)`，等待错误帧完成发送后关闭连接，不发送 ACK。

BAD_PARAM、BAD_STATE 和 UNKNOWN_CMD 不关闭连接，也不能改变原会话状态。

### 11.3 用明确结果控制连接生命周期

帧处理函数适合返回明确动作，而不是在深层辅助函数中直接关闭 Socket：

~~~cpp
enum class FrameAction
{
    KeepConnection,
    CloseConnection,
    FatalError,
};
~~~

- `KeepConnection`：当前帧处理完成，可继续同一连接；
- `CloseConnection`：已经发送协议要求的最终响应，正常结束连接；
- `FatalError`：编码或发送失败，无法保证双方状态一致。

## 12. 关闭连接

会话结束时由 `SocketHandle` 调用 `closesocket()` 即可。`shutdown()` 是可选的连接方向控制，不是释放句柄的替代品。

如果应用要表达“本端已经发送完，但仍愿意接收对端剩余数据”，可以先半关闭发送方向：

~~~cpp
if (::shutdown(client.get(), SD_SEND) == SOCKET_ERROR)
{
    const int error = ::WSAGetLastError();
}

// 如协议需要，再继续 recv()，直到对端返回 0 或发生错误。
client.reset(); // 最终仍由 closesocket() 释放句柄
~~~

注意：

- `shutdown()` 不释放句柄；最终仍要 `closesocket()`。
- 监听 Socket 和客户端 Socket 是两个不同句柄，要分别关闭。
- `WSACleanup()` 必须晚于所有 Socket 的关闭。
- `send_all()` 成功只证明字节已被本机 Winsock 接受，不证明对端应用已经读取并处理；
- 协议要求“先发 ERROR，再关闭”时，至少要等 `send_all()` 成功，不能在仍有本地未提交字节时提前结束；
- 是否等待对端确认或执行完整的优雅关闭，必须由应用协议明确约定，不能从 TCP API 返回值臆测。

## 13. 阻塞式单客户端主循环

当前项目使用的结构可以概括为：

~~~text
创建 WinsockRuntime
创建、bind、listen 监听 Socket

client = accept(listener)
创建本连接专属的 StreamParser
session_state = AwaitHello

session_loop:
    received = recv(client)

    if received > 0:
        frames = parser.feed(received_bytes)
        for frame in frames:
            action = process_frame(frame, session_state)
            if action == KeepConnection:
                continue
            if action == CloseConnection:
                正常结束本连接
            if action == FatalError:
                以失败结束本连接
        continue

    if received == 0:
        正常结束本连接

    记录 WSAGetLastError()
    以失败结束本连接

关闭 client
关闭 listener
销毁 WinsockRuntime
~~~

这种模型简单、调用栈清楚，但任一阻塞调用都会占用当前线程，而且同一时刻只服务一个连接。需要一个线程管理多个连接时，可切换到第 4.17 节的 `select()` 循环。

## 14. Winsock I/O 模型怎么选择

| 模型 | 常用函数 | 特点 | 适合场景 |
| --- | --- | --- | --- |
| 阻塞 I/O | `accept` / `recv` / `send` | 最简单；调用可能长期等待 | 单连接、学习、工具程序 |
| 同步复用 | `select` | 单线程等待多个 Socket；默认每集合 64 项 | 少量连接、逻辑简单 |
| 同步复用 | `WSAPoll` | 数组式事件描述，比 `fd_set` 直观 | 少量到中等连接 |
| 窗口消息 | `WSAAsyncSelect` | 网络事件转成窗口消息，同时把 Socket 设为非阻塞 | 传统 Win32 GUI |
| 事件对象 | `WSAEventSelect` / `WSAWaitForMultipleEvents` | 网络事件映射到事件句柄 | 少量事件驱动连接 |
| 重叠 I/O | `WSARecv` / `WSASend` + `OVERLAPPED` | 操作异步完成，需要管理缓冲区生命周期 | 中高并发、自定义完成机制 |
| 完成端口 | `CreateIoCompletionPort` + 重叠 I/O | 高并发 Windows 服务端常用模型 | 大量连接、吞吐优先 |

选择原则：

- 不因“异步更高级”就跳过阻塞模型的生命周期、半包和部分发送基础；
- `select()` 适合讲清单线程多连接，但不适合作为无限扩容方案；
- Qt GUI 应优先理解 `QTcpSocket` 与事件循环，不要在 UI 线程中自行运行阻塞 Winsock 循环；
- 切换 I/O 模型只改变“何时调用收发函数”，不会消除协议定界、状态机、背压和错误处理。

### 14.1 `WSAAsyncSelect` 和 `WSAEventSelect` 的重要副作用

注册这两类事件通知时，Winsock 会把相关 Socket 置为非阻塞模式。之后的 `accept()`、`recv()`、`send()` 需要正确处理 `WSAEWOULDBLOCK`。取消事件关联和恢复阻塞模式也必须按对应 API 契约执行，不能只改变一个局部变量就认为 Socket 已恢复。

### 14.2 `WSARecv` / `WSASend` 不等于普通函数换名字

重叠 I/O 要求：

- `WSABUF` 指向的内存在操作完成前始终有效；
- `OVERLAPPED` 对象在操作完成前始终有效；
- 通过事件、完成例程或 IOCP 接收完成通知；
- 同一连接上的多个并发发送仍需定义应用层顺序；
- 关闭 Socket 时要处理尚未完成操作的取消和完成通知。

如果没有清楚定义这些对象的所有权与完成顺序，重叠 I/O 比阻塞模型更容易产生悬空缓冲区和竞态。

## 15. 常见错误清单

1. **忘记调用 `WSAStartup()`**：任何 Winsock API 都可能失败。
2. **把 `SOCKET` 存入 `int`**：在 64 位平台可能截断句柄。
3. **使用 `close()`**：Windows Socket 必须使用 `closesocket()`。
4. **只调用一次 `send()`**：可能产生截断帧。
5. **一次 `recv()` 直接调用 `ProtocolCodec::decode()`**：把 TCP 分块误当成协议帧边界。
6. **把端口的小端/大端规则用于协议帧**：端口使用网络字节序，VirtualBenth 帧字段按协议小端。
7. **连接之间复用同一个 `StreamParser`**：上一个客户端的半包可能污染新连接。
8. **错误时读取 `errno`**：Winsock 应使用 `WSAGetLastError()`。
9. **先释放 Winsock 再关闭 Socket**：资源销毁顺序错误。
10. **重复使用 `select()` 返回后的 `fd_set`**：集合已被改写为就绪子集，下一轮会漏掉其他 Socket。
11. **把所有连接永久放入 `writefds`**：Socket 通常一直可写，会让 `select()` 高频立即返回并空转。
12. **把 `exceptfds` 当成通用错误集合**：它主要表示带外数据或非阻塞连接失败。
13. **非阻塞调用遇到 `WSAEWOULDBLOCK` 就断开**：这通常只表示暂时不能完成，应等待下一次就绪通知。
14. **在 Windows TCP 服务端照搬 Unix 的 `SO_REUSEADDR`**：可能允许其他进程强行复用端口，导致不确定分发。
15. **认为 `send_all()` 成功等于对端已处理**：它只保证全部字节已提交给本机 Socket。
16. **修改 `FD_SETSIZE` 但各翻译单元定义不一致**：会破坏 `fd_set` 的布局契约。

## 16. 与 VirtualBenth 源码的对应关系

| 指南概念 | 当前源码位置 |
| --- | --- |
| Winsock 初始化与清理 | `device_simulator/src/WinsockRuntime.cpp` 中 `WinsockRuntime` |
| Socket RAII | 同文件匿名命名空间中的 `SocketHandle` |
| `socket → bind → listen → accept` | `TcpServer::run()` |
| `recv()` 与正常/异常断开 | `TcpServer::run()` 接收循环 |
| TCP 半包、粘包和重同步 | `protocol/src/stream_parser.cpp` |
| Frame 编解码和 CRC | `protocol/src/protocol_codec.cpp`、`protocol/src/crc16.cpp` |
| 部分发送 | `send_all()` |
| ACK、STATUS、ERROR 构造 | `send_success()`、`send_error()` |
| HELLO/START/STOP 状态机 | `process_frame()` 与连接局部 `session_state` |

当前 `TcpServer::run()` 没有调用 `select()`：它只有一个客户端，直接阻塞 `recv()` 更简单。若以后改成一个线程管理多个客户端，应同时新增：

- 监听 Socket 和客户端集合；
- 每连接独立 `StreamParser`；
- 每连接独立会话状态；
- 每连接待发送缓冲和 `writefds` 管理；
- 连接移除、错误清理和 `FD_SETSIZE` 容量处理。

只把 `recv()` 外面套一层 `select()`，并不能自动完成多客户端架构。

## 17. 常见 Winsock 错误码

| 错误码 | 常见场景 | 通常处理方向 |
| --- | --- | --- |
| `WSAEWOULDBLOCK` | 非阻塞 `accept/recv/send/connect` 暂不能完成 | 等待下一次就绪事件，不要忙等 |
| `WSAEINTR` | 阻塞调用被取消或中断 | 根据退出策略重试或结束 |
| `WSAEADDRINUSE` | `bind()` 地址或端口已占用 | 检查残留进程、端口配置和绑定选项 |
| `WSAEACCES` | 地址使用被拒绝、独占绑定冲突等 | 检查端口权限和 `SO_EXCLUSIVEADDRUSE` |
| `WSAECONNRESET` | 对端强制复位连接 | 清理该连接状态，不复用 Parser |
| `WSAECONNABORTED` | 本地或网络栈中止连接 | 记录并清理连接 |
| `WSAETIMEDOUT` | 连接或配置的收发操作超时 | 按应用协议判定重试或断开 |
| `WSAECONNREFUSED` | 客户端连接目标未监听或拒绝 | 检查地址、端口、防火墙和服务状态 |
| `WSAENOTSOCK` | 句柄不是有效 Socket | 检查生命周期、重复关闭和句柄覆盖 |
| `WSAEINVAL` | 参数、Socket 状态或 `select` 集合不合法 | 对照具体 API 检查前置条件 |
| `WSAENOBUFS` | 系统缓冲或资源不足 | 降低资源占用、限制连接和排队数据 |

同一个错误码在不同 API 上可能有不同触发条件。处理时必须同时记录：

- API 名称；
- `WSAGetLastError()` 数值；
- Socket 所处阶段；
- 本地和对端地址；
- 当前是阻塞还是非阻塞模式。

## 18. 常用函数按任务速查

| 任务 | 首选函数 |
| --- | --- |
| 初始化/结束 Winsock | `WSAStartup` / `WSACleanup` |
| 创建 Socket | `socket` 或需要扩展能力时的 `WSASocket` |
| 解析主机、服务和地址族 | `getaddrinfo` / `freeaddrinfo` |
| 文本与二进制 IP 转换 | `inet_pton` / `inet_ntop` |
| 主机序与网络序转换 | `htons/ntohs`、`htonl/ntohl` |
| 设置本地端点 | `bind` |
| 服务端监听 | `listen` / `accept` |
| 客户端连接 | `connect` |
| 阻塞收发 | `recv` / `send` |
| 完整发送应用缓冲 | 在 `send` 之上实现 `send_all` 或发送队列 |
| 半关闭连接方向 | `shutdown` |
| 释放 Socket | `closesocket` |
| 等待少量 Socket 就绪 | `select` |
| 数组式就绪等待 | `WSAPoll` |
| 设置非阻塞、查询可读量 | `ioctlsocket` |
| 设置/读取 Socket 选项 | `setsockopt` / `getsockopt` |
| 查询本地/对端地址 | `getsockname` / `getpeername` |
| 读取线程最近一次 Winsock 错误 | `WSAGetLastError` |
| 重叠收发 | `WSARecv` / `WSASend` |
| 高并发完成通知 | IOCP + 重叠 I/O |

## 19. 官方资料

- [Microsoft Learn：Getting started with Winsock](https://learn.microsoft.com/en-us/windows/win32/winsock/getting-started-with-winsock)
- [Microsoft Learn：select function](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-select)
- [Microsoft Learn：getaddrinfo function](https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfo)
- [Microsoft Learn：connect function](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-connect)
- [Microsoft Learn：Winsock2.h functions](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/)
- [Microsoft Learn：SO_REUSEADDR 与 SO_EXCLUSIVEADDRUSE](https://learn.microsoft.com/en-us/windows/win32/winsock/using-so-reuseaddr-and-so-exclusiveaddruse)
- [Microsoft Learn：Socket IOCTL operations](https://learn.microsoft.com/en-us/windows/win32/winsock/summary-of-socket-ioctl-opcodes-2)

