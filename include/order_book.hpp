#pragma once

#include <map>
#include <unordered_map>
#include <memory>
#include "order.hpp"
#include "price_level.hpp"

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
};
