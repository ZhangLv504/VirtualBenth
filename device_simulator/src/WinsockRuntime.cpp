#include "WinsockRuntime.h"

// Windows 头可能定义 min/max 宏，NOMINMAX 确保下面使用的 std::min 不被宏替换。
#define NOMINMAX
#include <winsock2.h>

#include "protocol/frame.h"
#include "protocol/protocol_codec.h"
#include "protocol/stream_parser.h"
#include "device_simulator/outbound_queue.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstring>
#include <deque>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // 当前 TCP 连接的内部控制状态。
    //
    // 它描述“下一条控制命令是否可执行”，不是 STATUS 帧的线上状态码；尤其
    // AwaitHello 在线上没有对应值，禁止把枚举值直接 static_cast 后发送。
    enum class SessionState
    {
        AwaitHello, // TCP 已连接但尚未完成 HELLO，只接受 HELLO
        Ready,      // HELLO 响应已发完，可以接受第一次 START
        Running,    // START 响应已发完，采集配置有效，可以接受 STOP
        Idle,       // STOP 响应已发完，采集已停止，可以再次 START
    };

    // 当前 TCP 连接内一次采集会话的可变上下文。
    //
    // START 的 ACK+STATUS 全部发送后整体初始化，STOP 的响应全部发送后清空；
    // 连接结束时随 TcpServer::run() 的局部作用域销毁，不会泄漏到下一条连接。
    struct SamplingContext
    {
        std::uint32_t sample_rate{0}; // START 指定的每通道每秒样本数；0 表示未配置
        std::uint8_t channel_mask{0}; // bit0～bit7 分别表示通道 0～7 是否启用
        std::array<std::uint32_t, 8> sequence_by_channel{}; // 各通道下一 DATA 序号

        // 原子地应用新 START 配置，并把所有通道序号恢复到新会话起点 0。
        void start(std::uint32_t new_sample_rate, std::uint8_t new_channel_mask) noexcept
        {
            sample_rate = new_sample_rate;
            channel_mask = new_channel_mask;
            sequence_by_channel.fill(0);
        }

        // 清除停止会话后的全部配置和序号，使旧采集数据不能污染下次 START。
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

    // process_frame() 交给连接事件循环执行的生命周期决策。
    // 业务函数只负责校验和入队，不直接关闭 Socket，因而可以先排空错误响应。
    enum class FrameAction
    {
        KeepConnection, // 当前帧响应已入队，可继续当前连接
        CloseAfterFlush, // 响应全部发送后关闭当前连接
        FatalError,      // 编码或响应入队失败，无法可靠继续会话
    };

    // PendingTransition 对采样上下文的延迟操作。
    // 它与目标会话状态分开表示，因为 HELLO 只改状态，START/STOP 还要改上下文。
    enum class SamplingUpdate
    {
        None,  // 不改变 SamplingContext，例如 HELLO 成功
        Start, // 用 PendingTransition 中的参数开始新采集会话
        Reset, // 清空已经停止的采集会话
    };

    // 成功控制命令的状态变更不能在响应刚入队时提交。只有 ACK 与 STATUS 的
    // 全部字节都被 send() 接受后，事件循环才应用这里保存的待提交变更。
    struct PendingTransition
    {
        SessionState target_state{SessionState::AwaitHello}; // 响应排空后提交的状态
        SamplingUpdate sampling_update{SamplingUpdate::None}; // 同时执行的上下文操作
        std::uint32_t sample_rate{0}; // SamplingUpdate::Start 使用的采样率
        std::uint8_t channel_mask{0}; // SamplingUpdate::Start 使用的通道掩码
    };

    // 除 WSAStartup() 外，多数 Winsock API 用特殊返回值报告失败，再通过
    // WSAGetLastError() 获取线程最近一次 Winsock 错误码。
    // 此重载必须紧跟失败 API 调用，避免后续 Winsock 调用覆盖线程错误码。
    void report_winsock_error(const char* operation)
    {
        std::cerr << operation << " failed, WSA error=" << WSAGetLastError() << '\n';
    }

    // 已经提前保存错误码时使用本重载，避免日志阶段再次读取被覆盖的 last error。
    void report_winsock_error(const char* operation, int error_code)
    {
        std::cerr << operation << " failed, WSA error=" << error_code << '\n';
    }

    // 把业务层 Frame 编成完整线上字节，并把所有失败统一记录为日志 + false。
    // 先编码到局部结果，成功后再移动给 bytes，保证失败时调用方不会得到半帧。
    [[nodiscard]] bool encode_frame(
        const Frame& frame,
        std::vector<std::uint8_t>& bytes)
    {
        EncodeResult encoded = ProtocolCodec::encode(frame);
        if (!encoded.ok())
        {
            std::cerr << "ProtocolCodec::encode failed, error="
                      << static_cast<int>(encoded.error) << '\n';
            return false;
        }

        bytes = std::move(encoded.bytes);
        return true;
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

    // 构造控制命令成功确认。payload[0] 回显原命令，payload[1] 固定为 OK。
    [[nodiscard]] Frame make_ack_frame(Command command)
    {
        Frame ack_frame;
        ack_frame.command = Command::Ack;
        ack_frame.payload = {static_cast<std::uint8_t>(command), kAckOk};
        return ack_frame;
    }

    // 构造单字节 STATUS；参数必须来自协议定义的 IDLE/READY/RUNNING 状态码。
    [[nodiscard]] Frame make_status_frame(std::uint8_t status)
    {
        Frame status_frame;
        status_frame.command = Command::Status;
        status_frame.payload = {status};
        return status_frame;
    }

    // 为一组控制响应预留总容量。若剩余空间不足，先丢弃所有尚未开始发送的
    // DATA；已部分发送的 DATA 和既有控制帧不可撤回，清理后仍不足则返回 false。
    [[nodiscard]] bool ensure_control_capacity(
        OutboundQueue& outbound_queue,
        std::size_t required_bytes)
    {
        if (required_bytes > outbound_queue.capacity_bytes())
        {
            return false;
        }
        if (required_bytes >
            outbound_queue.capacity_bytes() - outbound_queue.queued_bytes())
        {
            outbound_queue.discard_unsent_data();
        }
        return required_bytes <=
            outbound_queue.capacity_bytes() - outbound_queue.queued_bytes();
    }

    // 编码单个控制帧并移动入队。返回 false 表示编码失败或硬容量仍不足；
    // 实际 Socket 发送由事件循环统一完成。
    [[nodiscard]] bool enqueue_control_frame(
        OutboundQueue& outbound_queue,
        const Frame& frame)
    {
        std::vector<std::uint8_t> encoded;
        if (!encode_frame(frame, encoded))
        {
            return false;
        }
        return outbound_queue.enqueue_control(std::move(encoded));
    }

    // ERROR 只要成功进入控制队列即可；实际发送和部分发送进度由事件循环维护。
    [[nodiscard]] bool enqueue_error(
        OutboundQueue& outbound_queue,
        std::uint16_t error_code)
    {
        return enqueue_control_frame(outbound_queue, make_error_frame(error_code));
    }

    // ACK 与 STATUS 必须作为一个原子响应批次预留容量，避免只排入 ACK 后因容量
    // 不足丢失 STATUS。当前没有 DATA，但该规则也为后续 STOP 清队列保留正确边界。
    [[nodiscard]] bool enqueue_success(
        OutboundQueue& outbound_queue,
        Command command,
        std::uint8_t target_status)
    {
        std::vector<std::uint8_t> ack_bytes;
        std::vector<std::uint8_t> status_bytes;
        if (!encode_frame(make_ack_frame(command), ack_bytes) ||
            !encode_frame(make_status_frame(target_status), status_bytes))
        {
            return false;
        }

        const std::size_t required_bytes = ack_bytes.size() + status_bytes.size();
        if (!ensure_control_capacity(outbound_queue, required_bytes))
        {
            return false;
        }

        return outbound_queue.enqueue_control(std::move(ack_bytes)) &&
            outbound_queue.enqueue_control(std::move(status_bytes));
    }

    // 对 StreamParser 已验证过结构和 CRC 的完整帧执行业务语义检查。
    // 返回 FrameAction 而不是在此直接关闭 Socket，让连接循环统一决定生命周期。
    [[nodiscard]] FrameAction process_frame(
        OutboundQueue& outbound_queue,
        const Frame& frame,
        SessionState session_state,
        std::optional<PendingTransition>& pending_transition)
    {
        // decode 会保留 CRC 正确帧的原始版本；版本不匹配是业务错误，而非流同步错误。
        if (frame.version != kProtocolVersion)
        {
            return enqueue_error(outbound_queue, kErrorVersionMismatch)
                ? FrameAction::CloseAfterFlush
                : FrameAction::FatalError;
        }

        switch (frame.command)
        {
        case Command::Hello:
        {
            // V1 HELLO 的版本只存在于帧头中，payload 必须严格为空。
            if (!frame.payload.empty())
            {
                return enqueue_error(outbound_queue, kErrorBadParameter)
                    ? FrameAction::KeepConnection
                    : FrameAction::FatalError;
            }

            // HELLO 只允许在刚建立连接、尚未握手时执行。重复 HELLO 属于
            // BAD_STATE，但协议规定该错误不关闭连接，也不能改变当前状态。
            if (session_state != SessionState::AwaitHello)
            {
                return enqueue_error(outbound_queue, kErrorBadState)
                    ? FrameAction::KeepConnection
                    : FrameAction::FatalError;
            }

            if (enqueue_success(outbound_queue, frame.command, kStatusReady))
            {
                pending_transition = PendingTransition{
                    SessionState::Ready, SamplingUpdate::None, 0, 0};
                return FrameAction::KeepConnection;
            }
            return FrameAction::FatalError;
        }

        case Command::Start:
        {
            if (frame.payload.size() != 5)
            {
                return enqueue_error(outbound_queue, kErrorBadParameter)
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
                return enqueue_error(outbound_queue, kErrorBadParameter)
                    ? FrameAction::KeepConnection
                    : FrameAction::FatalError;
            }

            if (session_state != SessionState::Ready &&
                session_state != SessionState::Idle)
            {
                return enqueue_error(outbound_queue, kErrorBadState)
                    ? FrameAction::KeepConnection
                    : FrameAction::FatalError;
            }

            if (enqueue_success(outbound_queue, frame.command, kStatusRunning))
            {
                pending_transition = PendingTransition{
                    SessionState::Running,
                    SamplingUpdate::Start,
                    sample_rate,
                    channel_mask};
                return FrameAction::KeepConnection;
            }
            return FrameAction::FatalError;
        }

        case Command::Stop:
        {
            if (!frame.payload.empty())
            {
                return enqueue_error(outbound_queue, kErrorBadParameter)
                    ? FrameAction::KeepConnection
                    : FrameAction::FatalError;
            }

            if (session_state != SessionState::Running)
            {
                return enqueue_error(outbound_queue, kErrorBadState)
                    ? FrameAction::KeepConnection
                    : FrameAction::FatalError;
            }

            if (enqueue_success(outbound_queue, frame.command, kStatusIdle))
            {
                pending_transition = PendingTransition{
                    SessionState::Idle, SamplingUpdate::Reset, 0, 0};
                return FrameAction::KeepConnection;
            }
            return FrameAction::FatalError;
        }

        case Command::Data:
        case Command::Status:
        case Command::Ack:
        case Command::Error:
        {
            // DATA/STATUS/ACK/ERROR 不应由 host 发给 device；这些已定义但方向
            // 不合法的帧使用 BAD_STATE，且保持当前连接与会话状态不变。
            return enqueue_error(outbound_queue, kErrorBadState)
                ? FrameAction::KeepConnection
                : FrameAction::FatalError;
        }

        case Command::Invalid:
        default:
        {
            // 未定义的线上命令由业务层响应 UNKNOWN_CMD，连接保持可用。
            return enqueue_error(outbound_queue, kErrorUnknownCommand)
                ? FrameAction::KeepConnection
                : FrameAction::FatalError;
        }
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

    // accept() 继承的是阻塞语义。连续 DATA 前必须让当前客户端 Socket 非阻塞，
    // 这样 send()/recv() 即使在 select() 返回后遇到竞争，也只会报告
    // WSAEWOULDBLOCK，而不会卡住整个控制事件循环。
    u_long nonblocking = 1;
    if (ioctlsocket(client_socket.get(), FIONBIO, &nonblocking) == SOCKET_ERROR)
    {
        report_winsock_error("ioctlsocket(FIONBIO)");
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
    // [底层网络接收缓冲区] 每次调用非阻塞 recv() 时临时存放原始字节流，容量对应协议规定的 payload 最大合法大小
    std::array<char, kReceiveBufferSize> receive_buffer{};
    // [应用层发送队列] 保存已编码等待发往网络的协议帧，负责管理容量上限、发送优先级以及部分发送进度的断点续传
    OutboundQueue outbound_queue;
    // [待处理的合法协议帧队列] 存放 parser 解析且 CRC 校验通过的帧，将网络层的“字节解析”与业务层的“命令处理”解耦
    std::deque<Frame> pending_frames;
    // [暂存的业务状态变更指令] 保证响应与状态同步：START/STOP 等引发的状态变更不会立刻生效，
    // 而是暂存此处，直到对应的 ACK/STATUS 响应全部发送完毕（发送队列为空）后才真正应用
    std::optional<PendingTransition> pending_transition;
    // [优雅关闭] 为 true 时，停止把该 Socket 加入读监听（不接新命令）
    // 仅监听写事件以排空 outbound_queue 中的错误响应，全部发完后再主动断开连接
    bool close_after_flush = false;

    // 当前小步只搭建 DATA 节拍唤醒骨架，不生成真实 DATA。20ms 是临时周期；
    // 后续会根据 START.sample_rate 与每帧 cnt 计算真实截止时间。
    constexpr std::chrono::milliseconds kDataTickInterval{20};
    std::chrono::steady_clock::time_point next_data_deadline{};

    while (true)
    {
        // 一个输入帧的响应全部发送完成后，才提交它对应的业务状态。随后才处理
        // 下一输入帧，避免同一次 recv() 中的合包命令越过尚未完成的状态转换。
        if (outbound_queue.empty())
        {
            if (pending_transition.has_value())
            {
                const PendingTransition transition = *pending_transition;
                if (transition.sampling_update == SamplingUpdate::Start)
                {
                    sampling_context.start(
                        transition.sample_rate, transition.channel_mask);
                }
                else if (transition.sampling_update == SamplingUpdate::Reset)
                {
                    sampling_context.reset();
                }

                const SessionState previous_state = session_state;
                session_state = transition.target_state;
                pending_transition.reset();

                if (previous_state != SessionState::Running &&
                    session_state == SessionState::Running)
                {
                    next_data_deadline =
                        std::chrono::steady_clock::now() + kDataTickInterval;
                }
            }

            if (close_after_flush)
            {
                std::cout << "Closing client connection as required by the protocol\n";
                return true;
            }

            if (!pending_frames.empty())
            {
                Frame frame = std::move(pending_frames.front());
                pending_frames.pop_front();
                std::cout << "Processing complete frame CMD: "
                          << static_cast<int>(frame.command) << '\n';

                const FrameAction action = process_frame(
                    outbound_queue, frame, session_state, pending_transition);
                if (action == FrameAction::FatalError)
                {
                    return false;
                }
                if (action == FrameAction::CloseAfterFlush)
                {
                    close_after_flush = true;
                }
            }
        }

        // select() 会改写传入集合，因此每轮都重新构造集合。协议要求关闭连接时
        // 只排空已入队错误响应，不再读取新的输入。
        fd_set readfds{};
        FD_ZERO(&readfds);
        fd_set* readfds_ptr = nullptr;
        if (!close_after_flush)
        {
            FD_SET(client_socket.get(), &readfds);
            readfds_ptr = &readfds;
        }

        fd_set writefds{};
        FD_ZERO(&writefds);
        fd_set* writefds_ptr = nullptr;
        if (!outbound_queue.empty())
        {
            FD_SET(client_socket.get(), &writefds);
            writefds_ptr = &writefds;
        }

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

        const int ready = select(
            0, readfds_ptr, writefds_ptr, nullptr, timeout_ptr);

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
        // 读事件优先处理，使 STOP 等控制输入不会被持续可写事件推迟。
        if (readfds_ptr != nullptr &&
            FD_ISSET(client_socket.get(), &readfds))
        {
            // 非阻塞 Socket 在 select() 后仍可能因竞争返回 WSAEWOULDBLOCK；这不是
            // 会话失败，保留所有状态并等待下一轮可读通知即可。
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
                const int error_code = WSAGetLastError();
                if (error_code != WSAEWOULDBLOCK)
                {
                    report_winsock_error("recv", error_code);
                    return false;
                }
            }
            else
            {
                std::vector<std::uint8_t> chunk(
                    static_cast<std::size_t>(bytes_read));
                std::memcpy(chunk.data(), receive_buffer.data(), chunk.size());

                std::vector<Frame> frames = parser.feed(chunk);
                for (Frame& frame : frames)
                {
                    std::cout << "Received complete frame CMD: "
                              << static_cast<int>(frame.command) << '\n';
                    pending_frames.push_back(std::move(frame));
                }
            }
        }

        // 仅当本轮 select() 实际监听且报告可写时发送一次。一次 send() 的返回值
        // 交给 OutboundQueue 保存偏移；WSAEWOULDBLOCK 不丢帧也不忙等。
        if (writefds_ptr != nullptr &&
            FD_ISSET(client_socket.get(), &writefds))
        {
            const std::optional<OutboundChunk> chunk = outbound_queue.front();
            if (!chunk.has_value() || chunk->size == 0)
            {
                std::cerr << "outbound queue returned an invalid writable chunk\n";
                return false;
            }

            const int request_size = static_cast<int>(std::min(
                chunk->size, static_cast<std::size_t>(INT_MAX)));
            const int sent = send(
                client_socket.get(),
                reinterpret_cast<const char*>(chunk->data),
                request_size,
                0);

            if (sent == SOCKET_ERROR)
            {
                const int error_code = WSAGetLastError();
                if (error_code != WSAEWOULDBLOCK)
                {
                    report_winsock_error("send", error_code);
                    return false;
                }
            }
            else if (sent == 0)
            {
                std::cerr << "send made no progress; closing the client connection\n";
                return false;
            }
            else
            {
                outbound_queue.consume(static_cast<std::size_t>(sent));
            }
        }
    }
}
