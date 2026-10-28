#ifndef VIRTUALBENTH_PROTOCOL_CODEC_H
#define VIRTUALBENTH_PROTOCOL_CODEC_H

#include "frame.h"

// Frame 编码阶段可能产生的通用结构错误。
// START、DATA 等命令各自的 payload 语义不属于本枚举，由业务层在调用前校验。
enum class EncodeError : std::uint8_t
{
    None,               // 编码成功，bytes 中保存完整线上帧
    UnsupportedVersion, // Frame.version 不是本端能够主动发送的版本
    InvalidCommand,     // command 是 Invalid、未知值或预留值
    PayloadTooLarge,    // payload 超过协议规定的 4096B 上限
};

// ProtocolCodec::encode() 的值语义返回对象。
// 失败时只设置 error，bytes 保持为空，调用方不会拿到可被误发的半成品帧。
struct EncodeResult
{
    EncodeError error{EncodeError::None}; // 本次编码的状态
    std::vector<std::uint8_t> bytes;      // 成功时的完整帧：magic 到 CRC

    // 统一表达成功条件，避免调用方分别猜测 error 与 bytes 的组合。
    [[nodiscard]] bool ok() const noexcept
    {
        return error == EncodeError::None;
    }
};

// 精确一帧解码时可能产生的结构或完整性错误。
// 找 magic、缓存半包和流式重同步由 StreamParser 处理，不属于这里的错误状态。
enum class DecodeError : std::uint8_t
{
    None,              // 结构和 CRC 均有效，frame 可以交给业务层
    IncompleteFrame,   // 输入不足固定帧头，或不足 payload_len 声明的总帧长
    InvalidMagic,      // 输入起始两个字节不是固定 magic A5 5A
    PayloadTooLarge,   // 帧头声明的 payload_len 超过 4096B
    FrameSizeMismatch, // 输入多于声明总帧长；本接口不负责拆分粘连帧
    CrcMismatch,       // 帧尾 CRC 与对 version 至 payload 的重新计算值不一致
};

// ProtocolCodec::decode() 的值语义返回对象。
// 采用失败关闭策略：只有结构、长度和 CRC 全部通过才发布解码后的 Frame。
struct DecodeResult
{
    DecodeError error{DecodeError::IncompleteFrame}; // 默认表示尚无可信完整帧
    Frame frame{}; // 失败时保持默认无效值，成功时保留线上原始版本和命令

    // 只以 error 为成功依据；未知版本/命令仍可能是结构正确的成功解码结果。
    [[nodiscard]] bool ok() const noexcept
    {
        return error == DecodeError::None;
    }
};

// 无状态的单帧编解码器。
//
// 它只负责线上公共布局、长度和 CRC，不保存 TCP 分片状态，也不判断某条命令在
// 当前会话中能否执行。无状态设计使同一实例无需共享，两个操作均为静态函数。
class ProtocolCodec
{
public:
    // 将 Frame 编码为 magic + 头字段 + payload + CRC 的完整线上帧。
    // 先完成全部通用校验再分配和填充结果，失败时不会返回部分字节。
    static EncodeResult encode(const Frame& frame);

    // 将“恰好一帧”的字节解码为 Frame。检查顺序为固定头、magic、长度、精确
    // 帧长、CRC，任一步失败都不发布字段。未知版本/命令作为业务事件原样保留。
    static DecodeResult decode(const std::vector<std::uint8_t>& bytes);
};

#endif //VIRTUALBENTH_PROTOCOL_CODEC_H
