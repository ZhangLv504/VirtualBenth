#include "protocol/protocol_codec.h"

#include "protocol/crc16.h"

#include <cstddef>
#include <cstring>
#include <utility>

namespace
{
    // 使用白名单判断线上命令，防止 static_cast 构造出的未知或预留值通过校验。
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

// 编码固定布局：magic(2) + version(1) + command(1) + payload_len(2) + payload + CRC(2)。
EncodeResult ProtocolCodec::encode(const Frame& frame)
{
    EncodeResult result;

    // 先拒绝无法在线上协议中合法表示的 Frame，确保失败时不产生部分帧。
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

    // payload_len 对应线上 uint16 字段；容器长度和写入位置使用 std::size_t。
    const std::uint16_t payload_len = static_cast<std::uint16_t>(frame.payload.size());
    const std::size_t total_len = payload_len + 8;
    std::vector<std::uint8_t> bytes(total_len);
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
    // CRC 覆盖 version 到 payload，共 4 + payload_len 字节；不含 magic 和 CRC 自身。
    const std::uint16_t crc16 = crc16_modbus(bytes.data() + 2, 4 + payload_len);
    bytes[pos++] = static_cast<std::uint8_t>(crc16 & 0xFF);
    bytes[pos++] = static_cast<std::uint8_t>((crc16 >> 8) & 0xFF);

    result.bytes = std::move(bytes);
    result.error = EncodeError::None;

    return result;
}


DecodeResult ProtocolCodec::decode(const std::vector<std::uint8_t>& bytes)
{
    DecodeResult result;

    // 先保证固定 6B 帧头完整，后续才可安全读取 version、command 和 payload_len。
    if (bytes.size() < 6)
    {
        result.error = DecodeError::IncompleteFrame;
        return result;
    }

    // 完整帧解码只校验起始位置；在流中搜索 magic 是 StreamParser 的职责。
    if (bytes[0] != 0xA5 || bytes[1] != 0x5A)
    {
        result.error = DecodeError::InvalidMagic;
        return result;
    }

    // 多字节字段显式按小端还原，避免依赖主机字节序。
    const std::uint16_t payload_len = static_cast<std::uint16_t>(bytes[4])
        | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[5]) << 8);

    // 非法超长头必须立即拒绝，不能按照伪造长度继续等待 payload。
    if (payload_len > 4096)
    {
        result.error = DecodeError::PayloadTooLarge;
        return result;
    }

    // 本接口只接受精确一帧：少于声明长度是未完成，多出的字节交给 StreamParser 拆分。
    const std::size_t frame_size = 8 + static_cast<std::size_t>(payload_len);
    if (bytes.size() < frame_size)
    {
        result.error = DecodeError::IncompleteFrame;
        return result;
    }

    if (bytes.size() > frame_size)
    {
        result.error = DecodeError::FrameSizeMismatch;
        return result;
    }

    // 帧尾 CRC 按小端读取；重新计算时覆盖原始字节中的 version 到 payload。
    const std::size_t crc_offset = 6 + static_cast<std::size_t>(payload_len);
    const std::uint16_t expected_crc = static_cast<std::uint16_t>(bytes[crc_offset])
        | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[crc_offset + 1]) << 8);
    const std::uint16_t actual_crc = crc16_modbus(bytes.data() + 2, 4 + payload_len);
    if (expected_crc != actual_crc)
    {
        result.error = DecodeError::CrcMismatch;
        return result;
    }

    // CRC 通过后再解释版本与命令，避免把传输损坏误分类为语义错误。
    if (bytes[2] != kProtocolVersion)
    {
        result.error = DecodeError::UnsupportedVersion;
        return result;
    }

    const Command command = static_cast<Command>(bytes[3]);
    if (!is_valid_command(command))
    {
        result.error = DecodeError::InvalidCommand;
        return result;
    }

    // 只有全部校验通过后才构造并发布 Frame，失败路径始终保留默认无效值。
    Frame frame;
    frame.version = bytes[2];
    frame.command = command;
    // std::vector::assign(first, last) 可以使用两个迭代器表示一个范围，把该范围中的元素复制到当前 vector 中。
    // 范围是左闭右开 [first, last)
    frame.payload.assign(bytes.begin() + 6, bytes.begin() + crc_offset);

    result.frame = std::move(frame);
    result.error = DecodeError::None;
    return result;
}
