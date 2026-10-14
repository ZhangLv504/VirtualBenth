#ifndef VIRTUALBENTH_PROTOCOL_CODEC_H
#define VIRTUALBENTH_PROTOCOL_CODEC_H

#include "frame.h"

// 编码阶段只校验通用帧约束；START、DATA 等命令的 payload 语义由上层负责。
enum class EncodeError : std::uint8_t
{
    None,                 // 编码成功，无错误
    UnsupportedVersion,   // 协议版本不支持（例如 version 不符合当前规范）
    InvalidCommand,       // 命令字无效（例如使用了禁止发送的 Command::Invalid）
    PayloadTooLarge,      // 载荷长度超过协议限制（超过了 4096 字节的上限）
};

struct EncodeResult
{
    // 成功时 bytes 保存一帧完整线上数据；失败时 bytes 保持为空。
    EncodeError error{EncodeError::None};
    std::vector<std::uint8_t> bytes;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == EncodeError::None;
    }
};

// 完整帧解码错误。这里不包含找 magic、缓存半包和重同步等流式解析状态。
enum class DecodeError : std::uint8_t
{
    None,                  // 解码成功，无错误
    IncompleteFrame,       // 输入少于帧头或少于 payload_len 声明的完整帧长度
    InvalidMagic,          // 输入开头不是固定字节 0xA5 0x5A
    PayloadTooLarge,       // payload_len 超过协议上限 4096B
    FrameSizeMismatch,     // 输入包含声明帧长度之外的额外字节；decode 只接受精确一帧
    CrcMismatch,           // CRC 校验失败
    UnsupportedVersion,    // 协议版本不是当前支持的 0x01
    InvalidCommand,        // 命令不是协议定义的合法线上命令
};

struct DecodeResult
{
    // 失败关闭：只有完成结构、CRC 和帧级语义校验后才能显式设为 None。
    DecodeError error{DecodeError::IncompleteFrame};
    // 解码失败时保持默认无效 Frame，避免调用方使用未经完整校验的数据。
    Frame frame{};

    [[nodiscard]] bool ok() const noexcept
    {
        return error == DecodeError::None;
    }
};

class ProtocolCodec
{
public:
    // 将内存 Frame 编码为完整线上帧，不负责命令 payload 的业务语义校验。
    static EncodeResult encode(const Frame& frame);

    // 只接受一帧精确字节；搜索 magic、缓存半包和消费粘包属于 StreamParser。
    static DecodeResult decode(const std::vector<std::uint8_t>& bytes);
};

#endif //VIRTUALBENTH_PROTOCOL_CODEC_H
