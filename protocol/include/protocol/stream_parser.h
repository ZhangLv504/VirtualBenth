#ifndef VIRTUALBENTH_STREAM_PARSER_H
#define VIRTUALBENTH_STREAM_PARSER_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "frame.h"

// 把任意分块到达的 TCP 字节流恢复成完整协议帧。
//
// 每个实例只能绑定一条有序字节流。buffer_ 跨 feed() 调用保存半个 magic、帧头或
// payload；遇到噪声、超长头或 CRC 错误时按 1B 滑动重新寻找下一帧，不把业务层
// 的未知版本/命令误判为同步错误。本类不负责超时，连接层决定何时 reset()。
class StreamParser
{
public:
    // 把 chunk 追加到连接缓存，并尽可能解析出 0～N 个完整帧。
    // 返回值只包含本次调用新完成的帧；未完成尾部继续留在 buffer_ 中。
    std::vector<Frame> feed(const std::vector<std::uint8_t>& chunk);

    // 清空尚未组成完整帧的缓存。通常在连接关闭、协议接收超时或显式放弃当前
    // 字节流时调用；已经由 feed() 返回的 Frame 不受影响。
    void reset() noexcept;

    // 返回尚未消费的原始字节数，供诊断和连接层的缓存/超时策略使用。
    [[nodiscard]] std::size_t buffered_size() const noexcept;

private:
    // 尚未组成可信完整帧的连接局部字节；成功帧一经返回即从这里精确移除。
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
