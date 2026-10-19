#ifndef VIRTUALBENTH_FRAME_H
#define VIRTUALBENTH_FRAME_H

#include <cstdint>
#include <vector>

// 命令枚举
enum class Command : std::uint8_t
{
    Invalid     = 0x00,     // 内存默认值；编码禁止发送，解码时可作为未知线上命令保留
    Hello       = 0x01,     // 版本协商
    Start       = 0x02,     // 启动采样
    Stop        = 0x03,     // 停止采样
    Data        = 0x04,     // 采样数据上报
    Status      = 0x05,     // 状态通知
    Ack         = 0x06,     // 控制命令处理成功确认
    Error       = 0x7F,     // 错误通知
};

// 协议版本常量
inline constexpr std::uint8_t kProtocolVersion = 0x01;

struct Frame
{
    std::uint8_t version{kProtocolVersion};     // 协议版本
    Command command{Command::Invalid};          // 命令字(默认不可发送)
    std::vector<std::uint8_t> payload;          // 载荷数据
};

#endif //VIRTUALBENTH_FRAME_H
