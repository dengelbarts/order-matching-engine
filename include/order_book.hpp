#pragma once

#include <map>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include "order.hpp"
#include "price_level.hpp"
#include "trade.hpp"
#include "events.hpp"

class OrderBook
{
    private:
        std::map<Price, PriceLevel, std::greater<Price>> bids_;
        std::map<Price, PriceLevel, std::less<Price>> asks_;
        std::unordered_map<OrderId, Order*> order_lookup_;

        std::function<void(const TradeEvent &)> on_trade_;
        std::function<void(const OrderEvent &)> on_order_event_;

        struct Stats
        {
            uint64_t total_orders = 0;
            uint64_t total_trades = 0;
            uint64_t total_volume = 0;
        } stats_;

    public:
        OrderBook() = default;
        ~OrderBook() = default;

        void set_trade_callback(std::function<void(const TradeEvent &)> cb);
        void set_order_callback(std::function<void(const OrderEvent &)> cb);

        const Stats &get_stats() const { return stats_; }

        void add_order(Order *order);

        const std::map<Price, PriceLevel, std::greater<Price>> &get_bids() const { return bids_; }
        const std::map<Price, PriceLevel, std::less<Price>> &get_asks() const { return asks_; }
        
        bool has_order(OrderId order_id) const;
        Order *get_order(OrderId order_id) const;

        bool cancel_order(OrderId order_id);

        struct BBO
        {
            Price price;
            Quantity quantity;
            bool valid;
        };

        BBO get_best_bid() const;
        BBO get_best_ask() const;

        struct Spread
        {
            Price value;
            bool valid;
        };

        Spread get_spread() const;

        std::vector<Trade> match(Order *order);
};
