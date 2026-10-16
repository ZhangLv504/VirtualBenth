#include "protocol/stream_parser.h"

#include "protocol/protocol_codec.h"

#include <utility>

namespace
{
    constexpr std::uint8_t kMagicFirst = 0xA5;
    constexpr std::uint8_t kMagicSecond = 0x5A;
    constexpr std::size_t kHeaderSize = 6;
    constexpr std::size_t kFrameOverhead = 8;
    constexpr std::uint16_t kMaxPayloadSize = 4096;
}

std::vector<Frame> StreamParser::feed(const std::vector<std::uint8_t>& chunk)
{
    buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());
    std::vector<Frame> frames;

    while (true)
    {
        // 帧字段偏移都以 magic 为起点，因此解析帧头前必须先把 magic 移到 buffer_[0]。
        std::size_t magic_idx = 0;
        for (; magic_idx + 1 < buffer_.size(); magic_idx++)
        {
            if (buffer_[magic_idx] == kMagicFirst && buffer_[magic_idx + 1] == kMagicSecond)
            {
                break;
            }
        }

        if (magic_idx + 1 >= buffer_.size())
        {
            // 最后 1B 可能与下一批数据组成跨 chunk 的 magic，其他噪声可以丢弃。
            if (buffer_.size() > 1)
            {
                buffer_.erase(buffer_.begin(), buffer_.end() - 1);
            }
            return frames;
        }

        if (magic_idx > 0)
        {
            buffer_.erase(buffer_.begin(), buffer_.begin() + magic_idx);
        }

        if (buffer_.size() < kHeaderSize)
        {
            return frames;
        }

        const std::uint16_t payload_len = static_cast<std::uint16_t>(buffer_[4])
            | static_cast<std::uint16_t>(static_cast<std::uint16_t>(buffer_[5]) << 8);
        if (payload_len > kMaxPayloadSize)
        {
            // 当前 magic 可能是伪起点；只滑动 1B，避免跳过后方真正的合法帧。
            buffer_.erase(buffer_.begin());
            continue;
        }

        const std::size_t frame_size = kFrameOverhead + static_cast<std::size_t>(payload_len);
        if (buffer_.size() < frame_size)
        {
            return frames;
        }

        // CRC 和完整帧结构由 ProtocolCodec 统一判断，StreamParser 只负责定界与重同步。
        const std::vector<std::uint8_t> candidate(buffer_.begin(), buffer_.begin() + frame_size);
        DecodeResult result = ProtocolCodec::decode(candidate);
        if (!result.ok())
        {
            buffer_.erase(buffer_.begin());
            continue;
        }

        frames.push_back(std::move(result.frame));
        // 成功后只消费精确帧长，保留其后可能粘连的下一帧并继续解析。
        buffer_.erase(buffer_.begin(), buffer_.begin() + frame_size);
    }
}

void StreamParser::reset() noexcept
{
    buffer_.clear();
}

std::size_t StreamParser::buffered_size() const noexcept
{
    return buffer_.size();
}
