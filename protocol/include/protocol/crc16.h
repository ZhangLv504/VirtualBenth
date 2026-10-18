#pragma once

#include <cstddef>
#include <cstdint>

// CRC16-Modbus
// 多项式 0x8005（反射实现 0xA001），初值 0xFFFF，无最终异或。
// 覆盖范围：帧的 ver ~ payload 共 4 + payload_len 个字节，不含 magic 与 crc16 本身。
// 计算结果按小端写入帧尾（低字节在前）。
uint16_t crc16_modbus(const uint8_t* data, std::size_t len);
