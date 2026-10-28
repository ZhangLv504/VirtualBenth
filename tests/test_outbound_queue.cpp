#include "catch_amalgamated.hpp"

#include "device_simulator/outbound_queue.h"

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace
{
    // 用可读的十六进制初始化列表构造最小测试帧，避免各用例重复声明临时 vector；
    // 这些字节只用于验证队列顺序和容量，不要求本身是合法协议帧。
    std::vector<std::uint8_t> bytes(std::initializer_list<std::uint8_t> values)
    {
        return std::vector<std::uint8_t>(values);
    }
}

TEST_CASE("control frames overtake data that has not started",
          "[simulator][outbound-queue][priority]")
{
    OutboundQueue queue(16);
    REQUIRE(queue.enqueue_data(bytes({0xD1, 0xD2, 0xD3})));
    REQUIRE(queue.enqueue_control(bytes({0xC1, 0xC2})));

    const auto control = queue.front();
    REQUIRE(control.has_value());
    REQUIRE(control->kind == OutboundFrameKind::Control);
    REQUIRE(control->size == 2);
    REQUIRE(control->data[0] == 0xC1);

    queue.consume(control->size);
    const auto data = queue.front();
    REQUIRE(data.has_value());
    REQUIRE(data->kind == OutboundFrameKind::Data);
    REQUIRE(data->size == 3);
    REQUIRE(data->data[0] == 0xD1);
}

TEST_CASE("a partially sent data frame cannot be preempted",
          "[simulator][outbound-queue][partial-send]")
{
    OutboundQueue queue(16);
    REQUIRE(queue.enqueue_data(bytes({0xD1, 0xD2, 0xD3, 0xD4})));

    queue.consume(2);
    REQUIRE(queue.enqueue_control(bytes({0xC1, 0xC2})));

    const auto remaining_data = queue.front();
    REQUIRE(remaining_data.has_value());
    REQUIRE(remaining_data->kind == OutboundFrameKind::Data);
    REQUIRE(remaining_data->size == 2);
    REQUIRE(remaining_data->data[0] == 0xD3);

    queue.consume(remaining_data->size);
    const auto control = queue.front();
    REQUIRE(control.has_value());
    REQUIRE(control->kind == OutboundFrameKind::Control);
    REQUIRE(control->data[0] == 0xC1);
}

TEST_CASE("data backpressure leaves a bounded queue unchanged",
          "[simulator][outbound-queue][capacity]")
{
    OutboundQueue queue(6);
    REQUIRE(queue.enqueue_data(bytes({1, 2, 3, 4})));
    REQUIRE_FALSE(queue.enqueue_data(bytes({5, 6, 7})));

    REQUIRE(queue.capacity_bytes() == 6);
    REQUIRE(queue.queued_bytes() == 4);
    REQUIRE(queue.queued_frames() == 1);
    const auto queued_data = queue.front();
    REQUIRE(queued_data.has_value());
    REQUIRE(queued_data->data[0] == 1);

    OutboundQueue default_queue;
    REQUIRE(default_queue.capacity_bytes() == 64U * 1024U);
}

TEST_CASE("a frame leaves the queue only after all bytes are consumed",
          "[simulator][outbound-queue][completion]")
{
    OutboundQueue queue(8);
    REQUIRE(queue.enqueue_control(bytes({1, 2, 3})));

    queue.consume(1);
    REQUIRE(queue.queued_bytes() == 3);
    REQUIRE(queue.queued_frames() == 1);
    const auto remainder = queue.front();
    REQUIRE(remainder.has_value());
    REQUIRE(remainder->size == 2);
    REQUIRE(remainder->data[0] == 2);

    queue.consume(2);
    REQUIRE(queue.empty());
    REQUIRE(queue.queued_bytes() == 0);
    REQUIRE_FALSE(queue.front().has_value());
}

TEST_CASE("discarding unsent data preserves a partially sent frame",
          "[simulator][outbound-queue][discard]")
{
    OutboundQueue queue(16);
    REQUIRE(queue.enqueue_data(bytes({1, 2, 3, 4})));
    REQUIRE(queue.enqueue_data(bytes({5, 6, 7})));
    queue.consume(1);

    REQUIRE(queue.discard_unsent_data() == 3);
    REQUIRE(queue.queued_bytes() == 4);
    REQUIRE(queue.queued_frames() == 1);
    const auto remainder = queue.front();
    REQUIRE(remainder.has_value());
    REQUIRE(remainder->size == 3);
    REQUIRE(remainder->data[0] == 2);
}

TEST_CASE("control frames may reclaim capacity from unsent data",
          "[simulator][outbound-queue][control-capacity]")
{
    OutboundQueue queue(8);
    REQUIRE(queue.enqueue_data(bytes({1, 2, 3, 4, 5, 6})));
    REQUIRE(queue.enqueue_control(bytes({0xC1, 0xC2, 0xC3, 0xC4})));

    REQUIRE(queue.queued_bytes() == 4);
    REQUIRE(queue.queued_frames() == 1);
    const auto control = queue.front();
    REQUIRE(control.has_value());
    REQUIRE(control->kind == OutboundFrameKind::Control);
    REQUIRE(control->data[0] == 0xC1);
}
