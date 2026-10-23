#include "device_simulator/custom_queue.h"

void CustomQueue::push(const std::uint8_t& data)
{
    pos_ = (pos_ + 1) % max_size_;
    buffer_[pos_] = data;
}

void CustomQueue::pop()
{
    start = (start + 1) % max_size_;
}

std::uint8_t CustomQueue::front()
{
    return buffer_[start];
}

std::uint8_t CustomQueue::back()
{
    return buffer_[pos_];
}

int CustomQueue::size()
{
    return pos_ - start;
}

bool CustomQueue::empty()
{
    return pos_ == start;
}
