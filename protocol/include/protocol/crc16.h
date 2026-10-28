#pragma once

#include <cstddef>
#include <cstdint>

// 计算一段连续字节的 CRC16-Modbus。
//
// 算法参数：多项式 0x8005（反射实现 0xA001）、初值 0xFFFF、无最终异或。
// 协议层调用时应传入 ver 至 payload 的连续区间，共 4 + payload_len 字节；magic
// 和帧尾 CRC 不在覆盖范围内。返回的是主机整数值，调用方负责按小端写入帧尾。
// 当 len > 0 时 data 必须指向至少 len 字节的可读内存；本函数不负责空指针检查。
uint16_t crc16_modbus(const uint8_t* data, std::size_t len);
