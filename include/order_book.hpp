#pragma once

#include "events.hpp"
#include "object_pool.hpp"
#include "order.hpp"
#include "price_level.hpp"
#include "trade.hpp"

#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <unordered_map>
#include <vector>

class OrderBook {
private:
    // pool_resource_ must be declared before bids_/asks_ — member initialisation
    // follows declaration order, so the pool exists before the maps reference it.
    std::pmr::unsynchronized_pool_resource pool_resource_;

    std::pmr::map<Price, PriceLevel, std::greater<Price>> bids_{
        std::greater<Price>{}, &pool_resource_};
    std::pmr::map<Price, PriceLevel, std::less<Price>> asks_{
        std::less<Price>{}, &pool_resource_};

    std::unordered_map<OrderId, Order*> order_lookup_;

    std::function<void(const TradeEvent&)> on_trade_;
    std::function<void(const OrderEvent&)> on_order_event_;

    struct Stats {
        uint64_t total_orders = 0;
        uint64_t total_trades = 0;
        uint64_t total_volume = 0;
    } stats_;

    ObjectPool<Order, 1 << 19> pool_;
    std::vector<Trade> scratch_trades_;

    bool can_fill(const Order* order) const;
    void match_impl(Order* order, std::vector<Trade>& out, bool fire_new_on_rest = true);

public:
    OrderBook() = default;
    ~OrderBook() = default;

    // Non-copyable and non-movable: unsynchronized_pool_resource cannot be
    // moved, so the whole class cannot be either.
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&) = delete;
    OrderBook& operator=(OrderBook&&) = delete;

    void set_trade_callback(std::function<void(const TradeEvent&)> cb);
    void set_order_callback(std::function<void(const OrderEvent&)> cb);

    const Stats& get_stats() const { return stats_; }

    void add_order(Order* order);

    const std::pmr::map<Price, PriceLevel, std::greater<Price>>& get_bids() const {
        return bids_;
    }
    const std::pmr::map<Price, PriceLevel, std::less<Price>>& get_asks() const {
        return asks_;
    }

    bool has_order(OrderId order_id) const;
    Order* get_order(OrderId order_id) const;

    bool cancel_order(OrderId order_id);

    bool amend_order(OrderId order_id, Quantity new_qty, Price new_price);

    struct BBO {
        Price price;
        Quantity quantity;
        bool valid;
    };

    BBO get_best_bid() const;
    BBO get_best_ask() const;

    struct Spread {
        Price value;
        bool valid;
    };

    Spread get_spread() const;

    struct MarketBBO {
        BBO bid;
        BBO ask;
    };

    MarketBBO get_bbo() const;

    struct DepthLevel {
        Price price;
        Quantity quantity;
    };

    struct Depth {
        std::vector<DepthLevel> bids;
        std::vector<DepthLevel> asks;
    };

    Depth get_depth(size_t n) const;
    Depth get_snapshot() const;

    Order* create_order(OrderId id,
                        SymbolId sym,
                        TraderId trader,
                        Side side,
                        Price price,
                        Quantity qty,
                        Timestamp ts,
                        OrderType type);

    ObjectPool<Order, 1 << 19>::Stats get_pool_stats() const { return pool_.get_stats(); }

    std::vector<Trade> match(Order* order);
};
