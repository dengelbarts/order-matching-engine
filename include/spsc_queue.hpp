#pragma once

#include <atomic>
#include <cstddef>

template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2 and at least 2");

    static constexpr std::size_t MASK = Capacity - 1;

    struct alignas(64) PaddedIndex
    {
        std::atomic<std::size_t> value{0};
        char _pad[64 - sizeof(std::atomic<std::size_t>)]{};
    };
    static_assert(sizeof(PaddedIndex) == 64, "PaddedIndex must be exactly one cache line");

    T slots_[Capacity];
    PaddedIndex head_;
    PaddedIndex tail_;

    public:
        SpscQueue() = default;
        ~SpscQueue() = default;
        SpscQueue(const SpscQueue&) = delete;
        SpscQueue& operator=(const SpscQueue&) = delete;

        bool try_push(const T &item) noexcept
        {
            const std::size_t h = head_.value.load(std::memory_order_relaxed);
            const std::size_t next = (h + 1) & MASK;
            if (next == tail_.value.load(std::memory_order_acquire))
                return false;
            slots_[h] = item;
            head_.value.store(next, std::memory_order_release);
            return true;
        }

        bool try_pop(T &out) noexcept
        {
            const std::size_t t = tail_.value.load(std::memory_order_relaxed);
            if (t == head_.value.load(std::memory_order_acquire))
                return false;
            out = slots_[t];
            tail_.value.store((t + 1) & MASK, std::memory_order_release);
            return true;
        }

        bool empty() const noexcept
        {
            return head_.value.load(std::memory_order_acquire) == tail_.value.load(std::memory_order_acquire);
        }

        static constexpr std::size_t capacity() noexcept { return Capacity - 1; }
};