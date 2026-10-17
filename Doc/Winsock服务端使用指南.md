# Winsock TCP 服务端使用指南

> 适用项目：VirtualBenth `device_simulator`  
> 当前目标：使用 C++17、Winsock 2.2、单线程阻塞 I/O，实现单客户端 HELLO 最小闭环。  
> 边界：本文不引入 Qt Network、Asio、多线程、IOCP、周期 DATA 推流或自动重连。

## 1. 为什么第一版选择 Winsock

当前 `device_simulator/CMakeLists.txt` 已在 Windows 下链接 `ws2_32`，不需要新增第三方依赖。第一版只有一个客户端，阻塞的 `accept()` 和 `recv()` 已足够，也最适合学习以下基础问题：

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

第一版可以串行处理客户端：当前客户端断开后，再回到 `accept()` 等待下一个客户端。暂时不需要每个连接创建一个线程。

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
address.sin_port = ::htons(9000);
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
- `flags`：第一版传 `0`。

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

这个包装可以防止提前 `return` 或异常路径遗漏 `closesocket()`。第一版不需要把它设计成通用网络框架。

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

第一版只使用 IPv4，暂不同时处理 IPv6。

### 7.2 绑定回环地址

协议文档没有冻结端口号。端口属于应用配置，不属于帧协议。第一版建议：

- 只绑定 `127.0.0.1`，避免尚未完成认证和资源保护时暴露到局域网；
- 端口集中定义在一处，后续再改为命令行参数；
- 不要把端口写进 `Doc/协议文档.md` 的帧格式。

```cpp
constexpr unsigned short kListenPort = 9000; // 示例值，最终由应用配置决定

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

第一版只处理一个客户端，但监听队列仍可使用 `SOMAXCONN`。这不等于应用会并发处理多个连接。

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

阻塞模式下，`accept()` 会等待客户端连接。这是第一版预期行为，不需要为了避免阻塞立即创建线程。

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
    // 阻塞模式下第一版可直接记录错误并结束当前会话。
    log_socket_error("recv", error);
    break;
}

parser.reset();
```

说明：

- 接收缓冲区大小不需要等于最大帧长；`StreamParser` 会跨多次调用缓存半包。
- `received` 为正数时才可以构造对应范围。
- `recv() == 0` 不是错误，而是对端正常关闭连接。
- 第一版发生接收错误时关闭当前连接，再回到 `accept()` 即可。

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

## 11. 发送协议响应

业务层先构造 `Frame`，再交给 `ProtocolCodec::encode()`；网络层只负责发送编码结果。

### 11.1 HELLO 成功响应

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

### 11.2 HELLO 业务校验

`ProtocolCodec::decode()` 和 `StreamParser` 只保证帧结构与 CRC 正确。业务层仍要检查：

- `frame.version == kProtocolVersion`；
- `frame.command == Command::Hello`；
- HELLO payload 必须为空；
- 当前连接是否处于允许 HELLO 的状态。

版本不匹配时应发送 `ERROR(VER_MISMATCH)`，等待错误帧完成发送后关闭连接，不发送 ACK。

第一里程碑只实现 HELLO，START、STOP 和 DATA 暂不加入同一轮。

## 12. 关闭连接

会话结束时由 `SocketHandle` 调用 `closesocket()` 即可。若需要明确停止双向收发，可以先调用：

```cpp
::shutdown(client.get(), SD_BOTH);
client.reset();
```

注意：

- `shutdown()` 不释放句柄；最终仍要 `closesocket()`。
- 监听 Socket 和客户端 Socket 是两个不同句柄，要分别关闭。
- `WSACleanup()` 必须晚于所有 Socket 的关闭。
- 版本错误需要“先发送 ERROR，再关闭”，不能刚调用 `send()` 就立刻假设对端已收到；第一版至少应确保 `send_all()` 成功返回。

## 13. 建议的第一版主循环

以下是结构伪代码，不是要求一次性全部实现：

```text
创建 WinsockRuntime
创建、bind、listen 监听 Socket

loop:
    client = accept(listener)
    为 client 创建新的 StreamParser

    session_loop:
        received = recv(client)

        if received > 0:
            frames = parser.feed(received_bytes)
            for frame in frames:
                校验并处理 HELLO
                encode ACK + STATUS
                send_all
            continue

        if received == 0:
            break session_loop

        记录 WSAGetLastError()
        break session_loop

    关闭 client
    继续等待下一个客户端
```

## 14. 分三个小里程碑实现

### 里程碑 A：只完成监听生命周期

- `WSAStartup()` 成功；
- `socket()`、`bind()`、`listen()` 成功；
- `accept()` 能接到一个本地客户端；
- 客户端断开后句柄被关闭；
- 暂不解析协议。

### 里程碑 B：接收并解析 HELLO

- 每次 `recv()` 的数据交给 `StreamParser::feed()`；
- HELLO 被任意拆分时仍能解析；
- 只处理完整 Frame；
- 暂不发送响应。

### 里程碑 C：完整 HELLO 闭环

- 合法 HELLO 返回 ACK(HELLO, OK)；
- 紧接着返回 STATUS(READY)；
- 使用 `send_all()`；
- 版本不匹配返回 ERROR 后关闭；
- 不实现 START、STOP 和 DATA。

每个里程碑完成静态审查后，再单独申请构建和运行测试许可。

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
10. **一开始就加入线程、IOCP、心跳、重连和 DATA 推流**：扩大范围，难以判断基础生命周期是否正确。

## 16. 当前阶段不做什么

- 不支持多个客户端并发；
- 不使用非阻塞 Socket、`select()`、WSAPoll 或 IOCP；
- 不创建网络线程或消息队列；
- 不做周期 DATA 推流；
- 不做断线重连；
- 不做公网或局域网暴露；
- 不让 `protocol` 依赖 Winsock；
- 不提前实现 host_gui。

这些功能只有在当前单客户端 HELLO 闭环通过后，才按规划逐步加入。

