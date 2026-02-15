#pragma once

#include <map>
#include <vector>
#include <unordered_map>
#include <memory>
#include "order.hpp"
#include "price_level.hpp"
#include "trade.hpp"

class OrderBook
{
    private:
        std::map<Price, PriceLevel, std::greater<Price>> bids_;
        std::map<Price, PriceLevel, std::less<Price>> asks_;
        std::unordered_map<OrderId, Order*> order_lookup_;

    public:
        OrderBook() = default;
        ~OrderBook() = default;

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
