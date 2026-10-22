#include "WinsockRuntime.h"

// Windows 头可能定义 min/max 宏，NOMINMAX 确保下面使用的 std::min 不被宏替换。
#define NOMINMAX
#include <winsock2.h>

#include "protocol/frame.h"
#include "protocol/protocol_codec.h"
#include "protocol/stream_parser.h"

#include <algorithm>
#include <array>
#include <chrono>
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

    // 当前 TCP 连接中一次采集会话的配置与逐通道序号。
    // START 成功后整体提交；STOP 成功时清空；连接结束时随 run() 的局部作用域销毁。
    struct SamplingContext
    {
        std::uint32_t sample_rate{0};
        std::uint8_t channel_mask{0};
        std::array<std::uint32_t, 8> sequence_by_channel{};

        void start(std::uint32_t new_sample_rate, std::uint8_t new_channel_mask) noexcept
        {
            sample_rate = new_sample_rate;
            channel_mask = new_channel_mask;
            sequence_by_channel.fill(0);
        }

        void reset() noexcept
        {
            sample_rate = 0;
            channel_mask = 0;
            sequence_by_channel.fill(0);
        }
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

        // 当前最小实现不需要转移所有权。完全禁止复制/移动可确保始终只有一个
        // 对象负责关闭句柄，避免 double-close 或悬空句柄。
        SocketHandle(const SocketHandle&) = delete;
        SocketHandle& operator=(const SocketHandle&) = delete;
        SocketHandle(SocketHandle&&) = delete;
        SocketHandle& operator=(SocketHandle&&) = delete;

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
        SOCKET socket_;
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
    [[nodiscard]] FrameAction process_frame(
        SOCKET socket,
        const Frame& frame,
        SessionState& session_state,
        SamplingContext& sampling_context)
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
                // 只有 ACK 与 RUNNING STATUS 都发送成功后，才同时提交状态与采样配置。
                // 再次 START 会从新的采集会话开始，因此所有通道序号重新归零。
                sampling_context.start(sample_rate, channel_mask);
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
                // STOP 成功后不再保留上一采集会话的配置与序号。
                sampling_context.reset();
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

    // 阶段 5：阻塞等待一个客户端。
    // accept() 返回专用于当前会话的新 Socket；监听 Socket 只负责接受连接，
    // 不参与该客户端的数据收发。client_socket 后声明，所以离开作用域时先关闭。
    SocketHandle client_socket(accept(listen_socket.get(), nullptr, nullptr));
    if (!client_socket.valid())
    {
        report_winsock_error("accept");
        return false;
    }

    // 当前服务器状态
    SessionState session_state{SessionState::AwaitHello};
    SamplingContext sampling_context{};

    std::cout << "A client connected\n";

    // 阶段 6：为当前连接建立独立接收状态。
    // 解析器内部可能保存半个 magic、帧头或 payload，缓存只能属于这一条 TCP 流；
    // 连接结束时销毁解析器，避免残留字节污染未来连接。
    StreamParser parser;
    std::array<char, kReceiveBufferSize> receive_buffer{};

    // 当前小步只搭建 DATA 节拍唤醒骨架，不生成真实 DATA。20ms 是临时周期；
    // 后续会根据 START.sample_rate 与每帧 cnt 计算真实截止时间。
    constexpr std::chrono::milliseconds kDataTickInterval{20};
    std::chrono::steady_clock::time_point next_data_deadline{};

    while (true)
    {
        // select() 会改写传入集合，因此每轮都重新构造当前客户端的读集合。
        fd_set readfds{};
        FD_ZERO(&readfds);
        FD_SET(client_socket.get(), &readfds);

        // 非 Running 状态没有 DATA 截止时间，可以一直等控制字节；
        // Running 状态则只等到最近的单调时钟截止点。timeval 必须活到 select() 返回。
        timeval timeout{};
        timeval* timeout_ptr = nullptr;
        if (session_state == SessionState::Running)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_data_deadline)
            {
                // 当前只保留 tick 边界，不生成或发送 DATA。跳过已经错过的空 tick，
                // 避免事件循环为了追赶历史截止点而高速空转。
                do
                {
                    next_data_deadline += kDataTickInterval;
                }
                while (next_data_deadline <= now);
                continue;
            }

            const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
                next_data_deadline - now);
            timeout.tv_sec = static_cast<long>(remaining.count() / 1000000);
            timeout.tv_usec = static_cast<long>(remaining.count() % 1000000);
            timeout_ptr = &timeout;
        }

        const int ready = select(0, &readfds, nullptr, nullptr, timeout_ptr);

        // select 失败，立即读取 `WSAGetLastError()`
        if (ready == SOCKET_ERROR)
        {
            report_winsock_error("select");
            return false;
        }
        // select 等待超时，没有 Socket 就绪
        if (ready == 0)
        {
            // Running 的 DATA 截止时间到达。当前骨架不发送帧，只推进到下一个未来截止点；
            // 正式 DATA 实现不得在 ACK(START)+RUNNING 之前进入这里。
            const auto now = std::chrono::steady_clock::now();
            do
            {
                next_data_deadline += kDataTickInterval;
            }
            while (next_data_deadline <= now);
            continue;
        }
        // 三个集合中就绪 Socket 的总数

        // 判断 readfds 中可读集合中是否有 client_socket
        if (!FD_ISSET(client_socket.get(), &readfds))
        {
            // 当前集合只有一个客户端；防御性保留，避免未来扩展集合时在未就绪
            // Socket 上调用阻塞 recv()。
            continue;
        }

        // select() 已确认当前 Socket 可读；recv() 仍需区分数据、正常关闭和错误。
        const int bytes_read = recv(
            client_socket.get(),
            receive_buffer.data(),
            static_cast<int>(receive_buffer.size()),
            0);

        if (bytes_read == 0)
        {
            std::cout << "Client disconnected normally\n";
            return true;
        }
        if (bytes_read == SOCKET_ERROR)
        {
            report_winsock_error("recv");
            return false;
        }

        std::vector<std::uint8_t> chunk(static_cast<std::size_t>(bytes_read));
        std::memcpy(chunk.data(), receive_buffer.data(), chunk.size());

        // 解析数据块
        const std::vector<Frame> frames = parser.feed(chunk);
        for (const Frame& frame : frames)
        {
            std::cout << "Received complete frame CMD: "
                      << static_cast<int>(frame.command) << '\n';

            const SessionState state_before = session_state;
            const FrameAction action = process_frame(
                client_socket.get(), frame, session_state, sampling_context
                );
            if (action == FrameAction::FatalError)
            {
                return false;
            }
            if (action == FrameAction::CloseConnection)
            {
                std::cout << "Closing client connection as required by the protocol\n";
                return true;
            }

            // 只有成功 START 才会把状态从 Ready/Idle 改为 Running；此时 ACK 与
            // STATUS 已发送成功，随后才能启动首个 DATA 截止时间。
            if (state_before != SessionState::Running &&
                session_state == SessionState::Running)
            {
                next_data_deadline = std::chrono::steady_clock::now() + kDataTickInterval;
            }

            // STOP 成功后状态已经离开 Running。旧截止点可以保留但不再参与
            // select()；下一次 START 会重新建立新的采集节拍起点。
        }
    }
}
