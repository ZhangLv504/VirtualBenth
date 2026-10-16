#include "protocol/stream_parser.h"

#include "protocol/protocol_codec.h"

// 追加任意大小的数据块，并返回本次调用中解析出的 0～N 个完整帧。
std::vector<Frame> StreamParser::feed(const std::vector<std::uint8_t>& chunk)
{
    std::vector<Frame> frames;

    // 将 chunk 添加到 buffer_ 中
    buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());

    while (!buffer_.empty())
    {
        // 在 buffer_ 中搜索完整 magic [0xA5, 0x5A]
        bool found_magic = false;
        std::size_t magic_idx = 0;
        for (; magic_idx + 1 < buffer_.size(); magic_idx++)
        {
            if (buffer_[magic_idx] == 0xA5 && buffer_[magic_idx + 1] == 0x5A)
            {
                found_magic = true;
                break;
            }
        }

        if (!found_magic)
        {
            // 如果最后一个字节恰好是 0xA5
            if (buffer_.back() == 0xA5)
            {
                // 删除除了最后一个字节之外的所有数据
                buffer_.erase(buffer_.begin(), buffer_.end() - 1);
            }
            else
            {
                // 全是脏数据，直接清空
                buffer_.clear();
            }
            return frames; // 缓存不够完整帧头，返回等待下一次 feed
        }

        if (buffer_.size() < 6)
        {
            return frames;
        }

        // 版本和命令是否有效这边不处理，交给业务层处理


        auto payload_len = (static_cast<std::uint16_t>(buffer_[4])) | (static_cast<std::uint16_t>(buffer_[5]) << 8);
        if (payload_len > 4096)
        {
            buffer_.erase(buffer_.begin(), buffer_.begin() + 1);
            continue;
        }

        auto frame_size = payload_len +8;
        if (buffer_.size() < frame_size)
        {
            return frames;
        }

        std::vector<std::uint8_t> payload;
        payload.insert(payload.end(), buffer_.begin(), buffer_.begin() + frame_size);
        DecodeResult result = ProtocolCodec::decode(payload);
        if (result.ok())
        {
            frames.push_back(result.frame);
        }
        else
        {
            buffer_.erase(buffer_.begin(), buffer_.begin() + 1);
            continue;
        }

        buffer_.erase(buffer_.begin(), buffer_.begin() + frame_size);
    }

    return frames;
}

// 清空尚未组成完整帧的缓存；帧接收超时由连接层判定后调用本函数。
void StreamParser::reset() noexcept
{
    buffer_.clear();
}

// 获取缓存大小
std::size_t StreamParser::buffered_size() const noexcept
{
    return buffer_.size();
}
