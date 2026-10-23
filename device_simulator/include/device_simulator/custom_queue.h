#ifndef VIRTUALBENTH_CUSTOM_QUEUE_H
#define VIRTUALBENTH_CUSTOM_QUEUE_H
#include <queue>
#include <string>

class CustomQueue final
{
public:
    void push(const std::uint8_t& data);
    void pop();
    std::uint8_t front();
    std::uint8_t back();
    int size();
    bool empty();

private:
    // 缓存
    std::uint8_t buffer_[64 * 1024] = {};
    int pos_ = 0;
    int start = 0;
    int max_size_ = 64 * 1024;
};

#endif //VIRTUALBENTH_CUSTOM_QUEUE_H
