#ifndef VIRTUALBENTH_STREAM_PARSER_H
#define VIRTUALBENTH_STREAM_PARSER_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "frame.h"

class StreamParser
{
public:
    // 追加任意大小的数据块，并返回本次调用中解析出的 0～N 个完整帧。
    std::vector<Frame> feed(const std::vector<std::uint8_t>& chunk);

    // 清空尚未组成完整帧的缓存；帧接收超时由连接层判定后调用本函数。
    void reset() noexcept;

    // 获取缓存大小
    [[nodiscard]] std::size_t buffered_size() const noexcept;

private:
    std::vector<std::uint8_t> buffer_;
};

/*
feed() 核心流程伪代码：

将 chunk 全部追加到 buffer_
创建空的 frames

loop:
    在 buffer_ 中搜索完整 magic [0xA5, 0x5A]
    如果找不到：
        如果 buffer_ 非空，保留最后 1B（它可能是跨 chunk magic 的首字节），丢弃其余字节
        返回 frames
    如果 magic 前存在噪声，[0xA5, 0x5A] 前面存在其他字节数据：
        丢弃 magic 前的全部字节

    如果 buffer_.size() < 6：
        返回 frames                         // 固定帧头未收齐，缓存不变，等待下次 feed

    按小端读取 payload_len = buffer_[4] | (buffer_[5] << 8)
    如果 payload_len > 4096：
        丢弃 buffer_ 的第 1B
        continue                            // 当前 magic 是伪起点，重新搜索

    frame_size = 8 + payload_len
    如果 buffer_.size() < frame_size：
        返回 frames                         // 候选帧未收齐，不能 continue 空转

    candidate = buffer_ 的前 frame_size 字节
    result = ProtocolCodec::decode(candidate)
    如果 result 失败：
        丢弃 buffer_ 的第 1B
        continue                            // 结构或 CRC 错误，滑动后重新同步

    将 result.frame 加入 frames
    从 buffer_ 消费前 frame_size 字节
    continue                                // 尝试解析同一缓存中的下一帧
*/

#endif // VIRTUALBENTH_STREAM_PARSER_H
