#ifndef VIRTUALBENTH_FRAME_H
#define VIRTUALBENTH_FRAME_H

#include <cstdint>
#include <vector>

// 线上协议的命令字。
//
// 枚举的底层类型固定为 1B，数值必须与 Doc/协议文档.md 一致。方向约定中的
// host 指上位机，device 指模拟设备；接收端仍需结合当前会话状态校验命令方向。
enum class Command : std::uint8_t
{
    Invalid = 0x00, // 仅作内存默认值；编码器禁止发送，不代表线上合法命令
    Hello = 0x01,   // host -> device：建立会话并确认协议版本，V1 payload 为空
    Start = 0x02,   // host -> device：携带采样率和通道掩码，开始一次采集
    Stop = 0x03,    // host -> device：停止当前采集，V1 payload 为空
    Data = 0x04,    // device -> host：上报某通道的一段连续采样数据
    Status = 0x05,  // device -> host：通知 READY、RUNNING 或 IDLE 状态
    Ack = 0x06,     // device -> host：确认某条控制命令已在应用层处理成功
    Error = 0x7F,   // device -> host：报告错误码和可选描述，不等同于 ACK
};

// 本实现当前唯一支持和主动发送的协议版本。
inline constexpr std::uint8_t kProtocolVersion = 0x01;

// 一帧已经定界或准备编码的内存表示。
//
// 本结构只保存字段，不保证字段组合一定满足业务语义：例如 START payload 的长度、
// 命令方向和状态机合法性分别由业务层检查。解码成功只证明帧结构与 CRC 有效。
struct Frame
{
    std::uint8_t version{kProtocolVersion}; // 帧头版本；默认使用本端支持版本
    Command command{Command::Invalid};      // 帧头命令；默认值不可被编码发送
    std::vector<std::uint8_t> payload;      // 不含帧头和 CRC 的原始 payload 字节
};

#endif //VIRTUALBENTH_FRAME_H
