#pragma once

#include "matching_engine.hpp"
#include "order_command.hpp"
#include "spsc_queue.hpp"

#include <atomic>
#include <functional>
#include <thread>

class MatchingPipeline {
    static constexpr std::size_t QUEUE_CAPACITY = 1 << 16;

    SpscQueue<OrderCommand, QUEUE_CAPACITY> queue_;
    MatchingEngine engine_;
    std::thread matching_thread_;
    std::atomic<uint64_t> processed_{0};

    void matching_loop() {
        OrderCommand cmd;
        while (true) {
            while (!queue_.try_pop(cmd))
                ;

            if (cmd.type == CommandType::Shutdown)
                return;

            engine_.route(cmd);
            processed_.fetch_add(1, std::memory_order_relaxed);
        }
    }

public:
    MatchingPipeline() = default;
    ~MatchingPipeline() = default;
    MatchingPipeline(const MatchingPipeline&) = delete;
    MatchingPipeline& operator=(const MatchingPipeline&) = delete;

    void set_trade_callback(std::function<void(const TradeEvent&)> cb) {
        engine_.set_trade_callback(std::move(cb));
    }

    void set_order_callback(std::function<void(const OrderEvent&)> cb) {
        engine_.set_order_callback(std::move(cb));
    }

    void start() {
        matching_thread_ = std::thread([this] { matching_loop(); });
    }

    void submit(const OrderCommand& cmd) {
        while (!queue_.try_push(cmd))
            ;
    }

    void shutdown() {
        submit(OrderCommand::make_shutdown());
        if (matching_thread_.joinable())
            matching_thread_.join();
    }

    uint64_t processed() const noexcept { return processed_.load(std::memory_order_relaxed); }

    const MatchingEngine& get_engine() const noexcept { return engine_; }
};
