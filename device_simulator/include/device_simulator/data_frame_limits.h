#ifndef VIRTUALBENTH_DATA_FRAME_LIMITS_H
#define VIRTUALBENTH_DATA_FRAME_LIMITS_H

#include <cstdint>

// DATA 固定字段占 15B、每个 float32 样本占 4B；在 4096B payload 上限内最多 1020 个。
inline constexpr std::uint16_t kMaxSamplesPerDataFrame = 1020;

#endif // VIRTUALBENTH_DATA_FRAME_LIMITS_H
