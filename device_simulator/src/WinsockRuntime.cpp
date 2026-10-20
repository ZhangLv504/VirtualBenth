#include "WinsockRuntime.h"

// Windows 头可能定义 min/max 宏，NOMINMAX 确保下面使用的 std::min 不被宏替换。
#define NOMINMAX
#include <winsock2.h>

#include "protocol/frame.h"
#include "protocol/protocol_codec.h"
#include "protocol/stream_parser.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    // 这是当前 TCP 连接的内部业务状态，不是 STATUS 帧的线上状态码。
    // AwaitHello 在线上没有对应值，因此禁止直接 static_cast 后发送。
    enum class SessionState
    {
        AwaitHello,
        Ready,
        Running,
        Idle,
    };

    // “ACK 成功”“READY 状态”“错误类型”，而不是散落难以审查的裸十六进制数。
    // 0x00 表示 ACK 处理结果为 OK
    constexpr std::uint8_t kAckOk = 0x00;
    // 0x00 表示设备已完成握手、当前处于空闲且未采样状态
    constexpr std::uint8_t kStatusIdle = 0x00;
    // 0x01 表示设备状态为 READY，含义为 HELLO 成功，待命
    constexpr std::uint8_t kStatusReady = 0x01;
    // 0x02 表示设备正在采集中；当前里程碑只维护状态，尚不推送 DATA
    constexpr std::uint8_t kStatusRunning = 0x02;
    // 0x0001 对应 UNKNOWN_CMD 错误码，表示收到未定义的 cmd
    constexpr std::uint16_t kErrorUnknownCommand = 0x0001;
    // 0x0002 对应 VER_MISMATCH 错误码，表示协议版本不一致
    constexpr std::uint16_t kErrorVersionMismatch = 0x0002;
    // 0x0003 对应 BAD_PARAM 错误码，表示参数非法（采样率=0、mask=0 等）
    constexpr std::uint16_t kErrorBadParameter = 0x0003;
    // 0x0004 对应 BAD_STATE 错误码，表示状态非法（如未启动就 STOP）
    constexpr std::uint16_t kErrorBadState = 0x0004;
    // 4096 对应协议规定的 payload 的最大合法字节数
    constexpr std::size_t kReceiveBufferSize = 4096;

    // 匿名命名空间让本文件的辅助类型和函数拥有内部链接，不成为模块公开 API。

    // 一个 SOCKET 句柄的独占所有者。
    //
    // Winsock 用 INVALID_SOCKET 表示无效句柄，而不是使用 nullptr。run() 中任一
    // 检查失败都会直接 return，容器分配也可能抛异常；把 closesocket() 放在析构
    // 函数中，才能让正常返回、错误返回和异常展开共享同一条可靠清理路径。
    class SocketHandle final
    {
    public:
        // 接管传入的 Socket；允许接管 INVALID_SOCKET，便于调用后统一检查 valid()。
        explicit SocketHandle(SOCKET socket = INVALID_SOCKET) noexcept
            : socket_(socket)
        {
        }

        // closesocket() 只对有效句柄调用；析构函数不抛异常。
        ~SocketHandle() noexcept
        {
            if (socket_ != INVALID_SOCKET)
            {
                closesocket(socket_);
            }
        }

        // 禁止拷贝：句柄只有一个所有者，复制会导致 double-close。
        SocketHandle(const SocketHandle&) = delete;
        SocketHandle& operator=(const SocketHandle&) = delete;

        // 允许移动：转移所有权，源对象置为 INVALID_SOCKET，
        // 这样源对象析构时不会关闭已转移出去的句柄。
        // noexcept 很关键：vector 扩容时会优先用它（否则异常安全要回退到拷贝，
        // 而拷贝已删除，会导致 vector 扩容失败/编译报错）
        SocketHandle(SocketHandle&& other) noexcept
            : socket_(other.socket_)
        {
            other.socket_ = INVALID_SOCKET;
        }

        SocketHandle& operator=(SocketHandle&& other) noexcept
        {
            if (this != &other)
            {
                close();                          // 先释放自己原有的句柄
                socket_ = other.socket_;
                other.socket_ = INVALID_SOCKET;
            }
            return *this;
        }

        // 借用底层句柄调用 Winsock API，所有权仍保留在 SocketHandle 中。
        [[nodiscard]] SOCKET get() const noexcept
        {
            return socket_;
        }

        // socket()/accept() 失败时返回 INVALID_SOCKET，不能把它传给后续 API。
        [[nodiscard]] bool valid() const noexcept
        {
            return socket_ != INVALID_SOCKET;
        }

    private:
        void close() noexcept
        {
            if (socket_ != INVALID_SOCKET)
            {
                closesocket(socket_);
                socket_ = INVALID_SOCKET;
            }
        }

        SOCKET socket_;
    };

    // 客户端连接结构体
    struct ClientSession
    {
        SocketHandle socket;
        StreamParser parser;
        SessionState state{SessionState::AwaitHello};
        std::array<char, kReceiveBufferSize> receive_buffer{};
        std::vector<std::uint8_t> send_queue;
        bool close_after_flush{false};                // 协议要求关闭时先冲刷
    };

    enum class FrameAction
    {
        KeepConnection,  // 当前帧已处理，可继续接收后续帧
        CloseConnection, // 已按协议发送响应，当前连接必须关闭
        FatalError,      // 编码或网络发送失败，无法可靠继续会话
    };

    // 除 WSAStartup() 外，多数 Winsock API 用特殊返回值报告失败，再通过
    // WSAGetLastError() 获取线程最近一次 Winsock 错误码。
    void report_winsock_error(const char* operation)
    {
        std::cerr << operation << " failed, WSA error=" << WSAGetLastError() << '\n';
    }

    // 把一帧编码后的全部字节提交给 TCP。
    //
    // TCP 是字节流；一次 send() 成功只代表返回值数量的字节已被接受，不保证
    // 整个 vector 一次发完。因此循环维护 total_sent，并从尚未发送的位置继续。
    // Winsock 的长度参数是 int，所以即使当前协议帧最多只有 4104B，也保留
    // INT_MAX 分块边界，防止以后复用函数时发生 size_t 到 int 的危险窄化。
    [[nodiscard]] bool send_all(SOCKET socket, const std::vector<std::uint8_t>& bytes)
    {
        std::size_t total_sent = 0;
        while (total_sent < bytes.size())
        {
            const std::size_t remaining = bytes.size() - total_sent;
            const int request_size = static_cast<int>(std::min(remaining, static_cast<std::size_t>(INT_MAX)));
            // vector<uint8_t> 保存原始字节；send() 的历史接口要求 const char*。
            const char* current = reinterpret_cast<const char*>(bytes.data() + total_sent);

            const int sent = send(socket, current, request_size, 0);
            if (sent == SOCKET_ERROR)
            {
                report_winsock_error("send");
                return false;
            }
            if (sent == 0)
            {
                // 对非空请求没有取得进展时继续循环会永久空转，因此把会话视为失败。
                std::cerr << "send made no progress; closing the client connection\n";
                return false;
            }

            total_sent += static_cast<std::size_t>(sent);
        }

        return true;
    }

    // 网络层只发送完整协议帧：先由 ProtocolCodec 负责布局、长度和 CRC，再由
    // send_all() 处理 TCP 部分发送。编码失败时绝不能发送空或半成品缓冲区。
    [[nodiscard]] bool send_frame(SOCKET socket, const Frame& frame)
    {
        const EncodeResult encoded = ProtocolCodec::encode(frame);
        if (!encoded.ok())
        {
            std::cerr << "ProtocolCodec::encode failed, error="
                      << static_cast<int>(encoded.error) << '\n';
            return false;
        }

        return send_all(socket, encoded.bytes);
    }

    // 构造不带文本描述的最小 ERROR 帧。Frame 默认使用本端支持的协议版本，
    // 即使对端发来未知版本，也能通过当前编码器生成可发送的错误响应。
    [[nodiscard]] Frame make_error_frame(std::uint16_t error_code)
    {
        Frame error_frame;
        error_frame.command = Command::Error;
        // 协议字段使用小端；错误描述允许为空，因此最小 ERROR payload 为 2B。
        error_frame.payload = {
            static_cast<std::uint8_t>(error_code & 0xFF),
            static_cast<std::uint8_t>((error_code >> 8) & 0xFF),
        };
        return error_frame;
    }

    // ERROR 的构造与发送集中在一个入口，避免各业务分支重复端序处理。
    [[nodiscard]] bool send_error(SOCKET socket, std::uint16_t error_code)
    {
        return send_frame(socket, make_error_frame(error_code));
    }

    // HELLO/START/STOP 成功时必须先发送 ACK，再发布该命令完成后的目标 STATUS。
    // 目标状态码由调用方显式传入，避免把包含 AwaitHello 的内部枚举误当成线上值。
    // 任一帧发送失败都返回 false；调用方只有在两帧全部发送成功后才提交内部状态。
    [[nodiscard]] bool send_success(
        SOCKET socket,
        Command command,
        std::uint8_t target_status)
    {
        Frame ack_frame;
        ack_frame.command = Command::Ack;
        ack_frame.payload = {static_cast<std::uint8_t>(command), kAckOk};
        if (!send_frame(socket, ack_frame))
        {
            return false;
        }

        Frame status_frame;
        status_frame.command = Command::Status;
        status_frame.payload = {target_status};
        return send_frame(socket, status_frame);
    }


    // 对 StreamParser 已验证过结构和 CRC 的完整帧执行业务语义检查。
    // 返回 FrameAction 而不是在此直接关闭 Socket，让连接循环统一决定生命周期。
    [[nodiscard]] FrameAction process_frame(SOCKET socket, const Frame& frame, SessionState& session_state)
    {
        // decode 会保留 CRC 正确帧的原始版本；版本不匹配是业务错误，而非流同步错误。
        if (frame.version != kProtocolVersion)
        {
            return send_error(socket, kErrorVersionMismatch)
                ? FrameAction::CloseConnection
                : FrameAction::FatalError;
        }

        switch (frame.command)
        {
        case Command::Hello:
        {
            // V1 HELLO 的版本只存在于帧头中，payload 必须严格为空。
            if (!frame.payload.empty())
            {
                return send_error(socket, kErrorBadParameter)
                    ? FrameAction::KeepConnection
                    : FrameAction::FatalError;
            }

            // HELLO 只允许在刚建立连接、尚未握手时执行。重复 HELLO 属于
            // BAD_STATE，但协议规定该错误不关闭连接，也不能改变当前状态。
            if (session_state != SessionState::AwaitHello)
            {
                return send_error(socket, kErrorBadState)
                    ? FrameAction::KeepConnection
                    : FrameAction::FatalError;
            }

            if (send_success(socket, frame.command, kStatusReady))
            {
                session_state = SessionState::Ready;
                return FrameAction::KeepConnection;
            }
            return FrameAction::FatalError;
        }

        case Command::Start:
        {
            if (frame.payload.size() != 5)
            {
                return send_error(socket, kErrorBadParameter)
                    ? FrameAction::KeepConnection
                    : FrameAction::FatalError;
            }
            // START 的前 4B 是显式小端 sample_rate，不能依赖主机字节序或
            // 直接把 payload 内存解释成 uint32_t（还可能违反对齐要求）。
            const std::uint32_t sample_rate =
                static_cast<std::uint32_t>(frame.payload[0]) |
                (static_cast<std::uint32_t>(frame.payload[1]) << 8) |
                (static_cast<std::uint32_t>(frame.payload[2]) << 16) |
                (static_cast<std::uint32_t>(frame.payload[3]) << 24);
            const std::uint8_t channel_mask = frame.payload[4];
            if (sample_rate == 0 || channel_mask == 0)
            {
                return send_error(socket, kErrorBadParameter)
                    ? FrameAction::KeepConnection
                    : FrameAction::FatalError;
            }

            if (session_state != SessionState::Ready &&
                session_state != SessionState::Idle)
            {
                return send_error(socket, kErrorBadState)
                    ? FrameAction::KeepConnection
                    : FrameAction::FatalError;
            }

            if (send_success(socket, frame.command, kStatusRunning))
            {
                session_state = SessionState::Running;
                return FrameAction::KeepConnection;
            }
            return FrameAction::FatalError;
        }

        case Command::Stop:
        {
            if (!frame.payload.empty())
            {
                return send_error(socket, kErrorBadParameter)
                    ? FrameAction::KeepConnection
                    : FrameAction::FatalError;
            }

            if (session_state != SessionState::Running)
            {
                return send_error(socket, kErrorBadState)
                    ? FrameAction::KeepConnection
                    : FrameAction::FatalError;
            }

            if (send_success(socket, frame.command, kStatusIdle))
            {
                session_state = SessionState::Idle;
                return FrameAction::KeepConnection;
            }
            return FrameAction::FatalError;
        }

        case Command::Data:
        case Command::Status:
        case Command::Ack:
        case Command::Error:
            // DATA/STATUS/ACK/ERROR 不应由 host 发给 device；这些已定义但方向
            // 不合法的帧使用 BAD_STATE，且保持当前连接与会话状态不变。
            return send_error(socket, kErrorBadState)
                ? FrameAction::KeepConnection
                : FrameAction::FatalError;

        case Command::Invalid:
        default:
            // 未定义的线上命令由业务层响应 UNKNOWN_CMD，连接保持可用。
            return send_error(socket, kErrorUnknownCommand)
                ? FrameAction::KeepConnection
                : FrameAction::FatalError;
        }
    }
} // namespace

WinsockRuntime::WinsockRuntime()
{
    // WSAStartup() 是使用任何 Winsock API 的前置条件：必须先成功初始化 Winsock，
    // 之后才能调用任何 Winsock 函数。

    // WSADATA 用于接收 WSAStartup 成功调用后返回的 Winsock 实现细节，
    // 其中 wVersion 为实际协商到的版本，wHighVersion 为该实现支持的最高版本。
    WSADATA data{};

    // MAKEWORD(主版本, 副版本) 将两个字节打包成一个 WORD（16 位整数）。
    // MAKEWORD(2, 2) 即向系统请求使用 Winsock 2.2 版本。
    const WORD requested_version = MAKEWORD(2, 2);

    const int startup_result = WSAStartup(requested_version, &data);

    // WSAStartup 返回 0 表示成功，非 0 即为错误码本身。
    // 失败时 Winsock 尚未初始化，不会设置 "last error"，
    // 因此必须直接使用返回值，不能依赖 WSAGetLastError()。
    if (startup_result != 0)
    {
        throw std::runtime_error(
            "WSAStartup failed, error=" + std::to_string(startup_result));
    }

    // 校验实际协商到的版本是否为请求的 2.2：
    // 若运行时只提供更低版本，WSAStartup 可能返回成功但 wVersion != 2.2，
    // 此时继续执行会违反后续 API 的使用契约，故拒绝继续。
    if (data.wVersion != requested_version)
    {
        // 构造函数抛出后析构函数不会运行，因此这里必须手动撤销已成功的初始化，
        // 否则会泄漏 Winsock 的引用计数（每次成功 WSAStartup 都需配对的 WSACleanup）。
        WSACleanup();
        throw std::runtime_error("Winsock 2.2 is not available");
    }
}

WinsockRuntime::~WinsockRuntime() noexcept
{
    // 只有构造成功的对象才会执行析构，因此这里恰好对应一次成功的 WSAStartup()。
    WSACleanup();
}

TcpServer::TcpServer(std::uint16_t port) noexcept
    : port_(port)
{
}

bool TcpServer::run()
{
    // 阶段 1：创建监听 Socket。AF_INET 表示 IPv4，SOCK_STREAM + IPPROTO_TCP
    // 明确选择面向连接的 TCP 字节流。
    SocketHandle listen_socket(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!listen_socket.valid())
    {
        report_winsock_error("socket");
        return false;
    }

    // 阶段 2：构造本地监听地址。sockaddr_in 先零初始化，避免保留未定义填充数据。
    sockaddr_in address{};
    address.sin_family = AF_INET;
    // sockaddr_in 属于 TCP/IP 层，端口与 IPv4 地址均使用网络字节序。
    address.sin_port = htons(port_);
    // 首个闭环只允许本机客户端连接，避免把未完成的实验服务暴露到局域网。
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // 阶段 3：bind() 把“本地地址 + 端口”关联到监听 Socket。失败常见原因包括
    // 端口已占用、地址不可用或系统权限限制；失败后不能继续假装正在监听。
    if (bind(
            listen_socket.get(),
            reinterpret_cast<const sockaddr*>(&address),
            static_cast<int>(sizeof(address))) == SOCKET_ERROR)
    {
        report_winsock_error("bind");
        return false;
    }

    // 阶段 4：把已绑定 Socket 切换为监听状态。SOMAXCONN 让系统选择等待队列上限。
    if (listen(listen_socket.get(), SOMAXCONN) == SOCKET_ERROR)
    {
        report_winsock_error("listen");
        return false;
    }

    std::cout << "Server is listening on 127.0.0.1:" << port_ << "...\n";

    // 所有活跃连接
    std::vector<ClientSession> clients;

    while (true)
    {
        // 构造本次要监听的 fd 集合
        fd_set readfds{};
        // 完全清空传入的 fd_set 集合，将其内部表示 Socket 的标志位全部重置为 0。
        FD_ZERO(&readfds);
        // 将特定的 Socket 句柄（这里是 listen_socket.get()）注册/添加进 readfds 这个集合中。
        // 如果这个 listen_socket 有动静（对于监听 Socket 而言，“可读”通常意味着有新的客户端发起了 TCP 连接等待被 accept），请唤醒程序来处理。
        FD_SET(listen_socket.get(), &readfds);

        for (const auto& client : clients)
        {
            FD_SET(client.socket.get(), &readfds);
        }

        // 阻塞等待，直到任意一个 fd 就绪
        //    Windows: 第一个参数忽略，传 0
        //    timeout 传 nullptr 表示永久等待
        // readfds 传给 select 时是"所有可读集合"（你关心的全集），select 返回后变成"就绪集合"（实际可读的子集）。
        const int ready = select(0, &readfds, nullptr, nullptr, nullptr);
        if (ready == SOCKET_ERROR)
        {
            report_winsock_error("select");
            return false;
        }
        // ready == 0 只可能发生在 timeout != nullptr 时，这里不会出现

        // FD_ISSET 检查某个特定的 Socket 句柄是否被包含在给定的集合（fd_set）中
        // 通常在 select() 函数调用返回之后使用。
        if (FD_ISSET(listen_socket.get(), &readfds))
        {
            SOCKET raw = accept(listen_socket.get(), nullptr, nullptr);
            if (raw == INVALID_SOCKET)
            {
                report_winsock_error("accept");
                // 单个 accept 失败不影响已有连接，继续循环
            }
            else
            {
                ClientSession cs;
                cs.socket = SocketHandle(raw);        // 走移动赋值
                clients.push_back(std::move(cs));
                std::cout << "A client connected\n";
            }
        }

        // 遍历所有客户端，处理就绪的
        for (auto it = clients.begin(); it != clients.end();)
        {
            if (!FD_ISSET(it->socket.get(), &readfds))
            {
                // 本客户端本轮无数据
                ++it;
                continue;
            }
            // 是否需要移除该客户端
            bool drop = false;
            const int bytes_read = recv(it->socket.get(),it->receive_buffer.data(),
                    static_cast<int>(it->receive_buffer.size()),0);

            if (bytes_read == 0)
            {
                std::cout << "Client disconnected normally\n";
                drop = true;
            }
            else if (bytes_read == SOCKET_ERROR)
            {
                report_winsock_error("recv");
                // 单客户端错误不影响其他客户端
                drop = true;
            }
            else
            {
                // recv() 写入 char 缓冲区，而协议层使用 uint8_t。memcpy 按原始字节复制，
                // 不会受到实现中 char 是否为有符号类型的影响。
                std::vector<std::uint8_t> chunk(static_cast<std::size_t>(bytes_read));
                std::memcpy(chunk.data(), it->receive_buffer.data(), chunk.size());

                // 阶段 7：恢复协议帧。一次 feed() 可能返回 0 帧、1 帧或多帧；半包会
                // 保留在 parser 内部，粘包则在本次调用中拆成多个 Frame。
                const std::vector<Frame> frames = it->parser.feed(chunk);
                for (const Frame& frame : frames)
                {
                    std::cout << "Received complete frame CMD: "
                              << static_cast<int>(frame.command) << '\n';

                    // 阶段 8：逐帧执行业务响应。发送失败与协议要求关闭必须立即终止
                    // 当前批次，不能继续处理同一 recv() 中排在后面的 Frame。
                    const FrameAction action = process_frame(it->socket.get(), frame, it->state);
                    if (action == FrameAction::FatalError)
                    {
                        return false;
                    }
                    if (action == FrameAction::CloseConnection)
                    {
                        std::cout << "Closing client connection as required by the protocol\n";
                        return true;
                    }
                }
            }

            if (drop)
            {
                // erase 返回下一个迭代器
                it = clients.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}
