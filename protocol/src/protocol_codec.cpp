#include "protocol/protocol_codec.h"

#include "protocol/crc16.h"

#include <cstddef>
#include <cstring>
#include <utility>

namespace
{
    [[nodiscard]] constexpr bool is_valid_command(Command command) noexcept
    {
        switch (command)
        {
        case Command::Hello:
        case Command::Start:
        case Command::Stop:
        case Command::Data:
        case Command::Status:
        case Command::Ack:
        case Command::Error:
            return true;
        case Command::Invalid:
            return false;
        }

        // enum class 仍可能由 static_cast 构造出未定义或预留值。
        return false;
    }
} // namespace

EncodeResult ProtocolCodec::encode(const Frame& frame)
{
    EncodeResult result;
    if (frame.version != kProtocolVersion)
    {
        result.error = EncodeError::UnsupportedVersion;
        return result;
    }

    if (!is_valid_command(frame.command))
    {
        result.error = EncodeError::InvalidCommand;
        return result;
    }

    if (frame.payload.size() > 4096)
    {
        result.error = EncodeError::PayloadTooLarge;
        return result;
    }

    // 获取载荷大小
    const std::uint16_t payload_len = static_cast<std::uint16_t>(frame.payload.size());
    // 获取完整长度
    const std::size_t total_len = payload_len + 8;
    // 创建帧数组
    std::vector<std::uint8_t> bytes(total_len);
    // 当前写入位置
    std::size_t pos = 0;

    // magic 固定字节序列，不参与端序
    bytes[pos++] = 0xA5;
    bytes[pos++] = 0x5A;
    // ver
    bytes[pos++] = frame.version;
    // cmd
    bytes[pos++] = static_cast<std::uint8_t>(frame.command);
    // payload_len，小端：低字节在前
    bytes[pos++] = static_cast<std::uint8_t>(payload_len & 0xFF);
    bytes[pos++] = static_cast<std::uint8_t>((payload_len >> 8) & 0xFF);
    // payload
    if (payload_len > 0)
    {
        std::memcpy(bytes.data() + pos, frame.payload.data(), payload_len);
    }
    pos += payload_len;
    // crc16
    const std::uint16_t crc16 = crc16_modbus(bytes.data() + 2, 4 + payload_len);
    bytes[pos++] = static_cast<std::uint8_t>(crc16 & 0xFF);
    bytes[pos++] = static_cast<std::uint8_t>((crc16 >> 8) & 0xFF);

    result.bytes = std::move(bytes);
    result.error = EncodeError::None;

    return result;
}
