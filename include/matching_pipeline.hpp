#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include "order_book.hpp"
#include "order_command.hpp"
#include "spsc_queue.hpp"

class MatchingPipeline
{
    static constexpr std::size_t QUEUE_CAPACITY = 1 << 16;

    SpscQueue<OrderCommand, QUEUE_CAPACITY> queue_;
    OrderBook book_;
    std::thread matching_thread_;
    std::atomic<uint64_t> processed_{0};

    void matching_loop()
    {
        OrderCommand cmd;
        while (true)
        {
            while (!queue_.try_pop(cmd))
                ;

            if (cmd.type == CommandType::Shutdown)
                return;
            
            switch (cmd.type)
            {
                case CommandType::NewOrder:
                    book_.create_order(cmd.order_id, cmd.symbol_id, cmd.trader_id, cmd.side(), cmd.price, cmd.quantity, cmd.timestamp, cmd.order_type());
                    break;
                case CommandType::Cancel:
                    book_.cancel_order(cmd.order_id);
                    break;
                case CommandType::Amend:
                    book_.amend_order(cmd.order_id, cmd.new_qty, cmd.new_price);
                    break;
                default:
                    break;
            }

            processed_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    public:
        MatchingPipeline() = default;
        ~MatchingPipeline() = default;
        MatchingPipeline(const MatchingPipeline&) = delete;
        MatchingPipeline &operator=(const MatchingPipeline&) = delete;

        void set_trade_callback(std::function<void(const TradeEvent&)> cb)
        {
            book_.set_trade_callback(std::move(cb));
        }

        void set_order_callback(std::function<void(const OrderEvent&)> cb)
        {
            book_.set_order_callback(std::move(cb));
        }

        void start()
        {
            matching_thread_ = std::thread([this] {matching_loop(); });
        }

        void submit(const OrderCommand &cmd)
        {
            while (!queue_.try_push(cmd))
                ;
        }

        void shutdown()
        {
            submit(OrderCommand::make_shutdown());
            if (matching_thread_.joinable())
                matching_thread_.join();
        }

        uint64_t processed() const noexcept
        {
            return processed_.load(std::memory_order_relaxed);
        }

        const OrderBook &book() const noexcept { return book_; }
};