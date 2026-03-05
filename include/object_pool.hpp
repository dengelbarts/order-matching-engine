#pragma once

#include <cstddef>
#include <cstdio>
#include <memory>
#include <new>
#include <utility>

template <typename T, std::size_t Capacity = 4096> class ObjectPool {
    static_assert(Capacity > 0, "Pool capacity must be positive");

private:
    std::unique_ptr<std::byte[]> storage_;
    std::unique_ptr<T*[]> free_list_;
    std::size_t free_count_;

    std::size_t in_use_ = 0;
    std::size_t high_water_ = 0;
    std::size_t heap_fallbacks_ = 0;

    T* slot(std::size_t i) noexcept { return reinterpret_cast<T*>(storage_.get() + i * sizeof(T)); }

public:
    struct Stats {
        std::size_t capacity;
        std::size_t available;
        std::size_t in_use;
        std::size_t high_water_mark;
        std::size_t heap_fallbacks;
    };

    ObjectPool()
        : storage_(std::make_unique<std::byte[]>(sizeof(T) * Capacity)),
          free_list_(std::make_unique<T*[]>(Capacity)), free_count_(Capacity) {
        for (std::size_t i = 0; i < Capacity; i++)
            free_list_[i] = slot(i);
    }

    ~ObjectPool() = default;
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    template <typename... Args> T* allocate(Args&&... args) {
        T* ptr;
        if (free_count_ > 0) {
            ptr = free_list_[--free_count_];
            new (ptr) T(std::forward<Args>(args)...);
        } else {
            std::fprintf(stderr, "[ObjectPool] WARNING: pool exhausted, falling back to heap\n");
            ++heap_fallbacks_;
            ptr = new T(std::forward<Args>(args)...);
        }
        ++in_use_;
        if (in_use_ > high_water_)
            high_water_ = in_use_;
        return ptr;
    }

    void deallocate(T* ptr) {
        if (!ptr)
            return;
        if (is_from_pool(ptr)) {
            ptr->~T();
            free_list_[free_count_++] = ptr;
        } else {
            delete ptr;
        }
        --in_use_;
    }

    bool is_from_pool(const T* ptr) const noexcept {
        const auto* p = reinterpret_cast<const std::byte*>(ptr);
        const auto* lo = storage_.get();
        const auto* hi = storage_.get() + sizeof(T) * Capacity;
        return p >= lo && p < hi;
    }

    std::size_t available() const noexcept { return free_count_; }
    std::size_t capacity() const noexcept { return Capacity; }

    Stats get_stats() const noexcept {
        return {Capacity, free_count_, in_use_, high_water_, heap_fallbacks_};
    }
};