#pragma once

#include <cstdint>
#include <atomic>
#include <chrono>
#include <ostream>
#include "price.hpp"
#include "order_types.hpp"

using OrderId = uint64_t;
using SymbolId = uint32_t;
using Quantity = uint64_t;
using Timestamp = uint64_t;

struct Order
{
    OrderId order_id;
    Price price;
    Quantity quantity;
    Timestamp timestamp;

    SymbolId symbol_id;

    Side side;
    OrderType order_type;

    Order(OrderId id, SymbolId sym, Side s, Price p, Quantity q, Timestamp ts, OrderType type)
        : order_id(id)
        , price(p)
        , quantity(q)
        , timestamp(ts)
        , symbol_id(sym)
        , side(s)
        , order_type(type)
    {}

    Order()
        : order_id(0)
        , price(0)
        , quantity(0)
        , timestamp(0)
        , symbol_id(0)
        , side(Side::Buy)
        , order_type(OrderType::Limit)
    {}
};

static_assert(sizeof(Order) <= 64, "Order struct must fit in a cache line (64 bytes)");

inline OrderId generate_order_id()
{
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

inline Timestamp get_timestamp_ns()
{
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
    
    return static_cast<Timestamp>(nanos.count());
}

inline std::ostream& operator<<(std::ostream &os, const Order &order)
{
    os  << "Order{"
        << "id=" << order.order_id
        << ", symbol=" << order.symbol_id
        << ", side=" << to_string(order.side)
        << ", price=" << price_to_string(order.price)
        << ", qty=" << order.quantity
        << ", ts=" << order.timestamp
        << ", type=" << to_string(order.order_type)
        << "}";
    return os;
}