#include "protocol/crc16.h"

// 逐字节、逐位推进反射 CRC：先把输入字节并入低 8 位，再连续处理 8 个最低位。
// 最低位为 1 时，右移后与反射多项式 0xA001 异或；否则只右移。该实现优先保证
// 算法与协议参数直观对应，未来即使改为查表优化，也必须保持相同输入输出契约。
uint16_t crc16_modbus(const uint8_t* data, std::size_t len) {
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}
