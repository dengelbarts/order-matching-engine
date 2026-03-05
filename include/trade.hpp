#pragma once

#include "order.hpp"

#include <atomic>
#include <cstdint>

using TradeId = uint64_t;

struct Trade {
    TradeId trade_id;
    OrderId buy_order_id;
    OrderId sell_order_id;
    Price price;
    Quantity quantity;
    Timestamp timestamp;

    Trade(TradeId tid, OrderId buy_id, OrderId sell_id, Price p, Quantity q, Timestamp ts)
        : trade_id(tid), buy_order_id(buy_id), sell_order_id(sell_id), price(p), quantity(q),
          timestamp(ts) {}
};

inline TradeId generate_trade_id() {
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}
