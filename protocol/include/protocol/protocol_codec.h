#ifndef VIRTUALBENTH_PROTOCOL_CODEC_H
#define VIRTUALBENTH_PROTOCOL_CODEC_H

#include "frame.h"

enum class EncodeError : std::uint8_t
{
    None,                 // 编码成功，无错误
    UnsupportedVersion,   // 协议版本不支持（例如 version 不符合当前规范）
    InvalidCommand,       // 命令字无效（例如使用了禁止发送的 Command::Invalid）
    PayloadTooLarge,      // 载荷长度超过协议限制（超过了 4096 字节的上限）
};

struct EncodeResult
{
    EncodeError error{EncodeError::None};
    std::vector<std::uint8_t> bytes;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == EncodeError::None;
    }
};

class ProtocolCodec
{
public:
    static EncodeResult encode(const Frame& frame);
};

#endif //VIRTUALBENTH_PROTOCOL_CODEC_H
