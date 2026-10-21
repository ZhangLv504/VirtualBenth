#ifndef VIRTUALBENTH_WINSOCKRUNTIME_H
#define VIRTUALBENTH_WINSOCKRUNTIME_H

#include <cstdint>
#include <vector>

// 本文件只暴露模拟器启动所需的最小接口。
// Winsock、SOCKET、sockaddr_in 等 Windows 细节全部留在 .cpp 中，避免调用方
// 仅仅包含本头文件就依赖 <winsock2.h>，也避免把具体传输实现扩散到业务层。

// 进程级 Winsock 生命周期守卫。
//
// Windows 要求任何 Socket API 都必须位于一次成功的 WSAStartup() 与对应的
// WSACleanup() 之间。本类把这对调用绑定到对象生命周期：构造失败会抛异常，
// 构造成功后则保证析构时清理。main() 应先创建它，再创建或运行 TcpServer。
class WinsockRuntime final
{
public:
    // 请求 Winsock 2.2；初始化失败或系统没有协商到 2.2 时抛出 std::runtime_error。
    WinsockRuntime();

    // 只释放构造函数已经成功取得的那一次 Winsock 初始化计数。
    ~WinsockRuntime() noexcept;

    // Winsock 初始化按调用次数计数。禁止复制和移动可避免多个对象对同一次
    // WSAStartup() 重复执行 WSACleanup()，破坏进程级初始化计数。
    WinsockRuntime(const WinsockRuntime&) = delete;
    WinsockRuntime& operator=(const WinsockRuntime&) = delete;
    WinsockRuntime(WinsockRuntime&&) = delete;
    WinsockRuntime& operator=(WinsockRuntime&&) = delete;
};

// S7 的单客户端 TCP 服务端与 DATA 节拍事件循环骨架。
//
// 当前边界刻意保持简单：绑定本机 loopback、只接受一个客户端，并在当前线程
// 通过 select() 同时等待控制字节和 Running 状态下的下一 DATA 截止时间。
// 当前只搭建 tick 骨架，尚不生成 DATA；多客户端循环属于后续阶段。
class TcpServer final
{
public:
    // port 使用主机字节序保存；填充 sockaddr_in 时再通过 htons() 转为网络字节序。
    explicit TcpServer(std::uint16_t port = 12345, uint32_t sample_rate = 10, uint8_t chan_mask = 1) noexcept;

    // 依次完成 socket → bind → listen → accept → select → recv/process/send。
    // 返回 true：客户端正常断开，或服务端已按协议发送错误并主动关闭连接。
    // 返回 false：Winsock API、协议编码或响应发送失败。
    // 本函数为阻塞调用，直到当前唯一客户端会话结束才返回。
    [[nodiscard]] bool run();

private:
    // 保持主机字节序，便于日志输出和构造函数参数传递。
    std::uint16_t port_;
    // START命令的 采样率 和 通道 和 缓存
    std::uint32_t sample_rate_;
    std::uint8_t chan_mask_;
    std::uint32_t seqs_[8];
};

#endif // VIRTUALBENTH_WINSOCKRUNTIME_H
