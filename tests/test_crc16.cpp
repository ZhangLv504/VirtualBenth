#include "catch_amalgamated.hpp"

#include "protocol/crc16.h"

// CRC16-Modbus 标准自检向量：ASCII "123456789" -> 0x4B37
TEST_CASE("crc16 self-check vector", "[crc16][selfcheck]") {
    const uint8_t kCheck[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    REQUIRE(crc16_modbus(kCheck, sizeof(kCheck)) == 0x4B37);
}

// 以下取自《协议文档》§9 示例帧的 CRC 覆盖范围（ver ~ payload）
TEST_CASE("crc16 protocol-doc section9 vectors", "[crc16][doc]") {
    SECTION("HELLO 期望 0x1850") {
        const uint8_t v[] = {0x01, 0x01, 0x00, 0x00};
        REQUIRE(crc16_modbus(v, sizeof(v)) == 0x1850);
    }
    SECTION("START 期望 0x0C59") {
        const uint8_t v[] = {0x01, 0x02, 0x05, 0x00, 0x10, 0x27,
                             0x00, 0x00, 0x07};
        REQUIRE(crc16_modbus(v, sizeof(v)) == 0x0C59);
    }
    SECTION("STOP 期望 0xD8F1") {
        const uint8_t v[] = {0x01, 0x03, 0x00, 0x00};
        REQUIRE(crc16_modbus(v, sizeof(v)) == 0xD8F1);
    }
    SECTION("STATUS 期望 0x0DC9") {
        const uint8_t v[] = {0x01, 0x05, 0x01, 0x00, 0x02};
        REQUIRE(crc16_modbus(v, sizeof(v)) == 0x0DC9);
    }
    SECTION("DATA 期望 0xC2D4") {
        const uint8_t v[] = {0x01, 0x04, 0x1B, 0x00, 0x00, 0x00, 0x04, 0x00,
                             0x00, 0xB0, 0x80, 0xC1, 0xAF, 0x8E, 0x01, 0x00,
                             0x00, 0x03, 0x00, 0x00, 0x00, 0xC0, 0x3F, 0xCD,
                             0xCC, 0x4C, 0xBE, 0xCD, 0xCC, 0x4C, 0x3F};
        REQUIRE(crc16_modbus(v, sizeof(v)) == 0xC2D4);
    }
    SECTION("ERROR 期望 0xAC99") {
        const uint8_t v[] = {0x01, 0x7F, 0x12, 0x00, 0x02, 0x00, 0x76, 0x65,
                             0x72, 0x73, 0x69, 0x6F, 0x6E, 0x20, 0x6D, 0x69,
                             0x73, 0x6D, 0x61, 0x74, 0x63, 0x68};
        REQUIRE(crc16_modbus(v, sizeof(v)) == 0xAC99);
    }
}
