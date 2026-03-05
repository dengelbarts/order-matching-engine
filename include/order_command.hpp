#pragma once

#include "order.hpp"

enum class CommandType : uint8_t { NewOrder, Cancel, Amend, Shutdown };

struct alignas(64) OrderCommand {
    CommandType type = CommandType::Shutdown;
    uint8_t side_raw = 0;
    uint8_t otype_raw = 0;
    uint8_t _pad[5] = {};

    OrderId order_id = 0;
    SymbolId symbol_id = 0;
    TraderId trader_id = 0;
    Price price = 0;
    Quantity quantity = 0;
    Timestamp timestamp = 0;
    Price new_price = 0;
    Quantity new_qty = 0;

    Side side() const { return static_cast<Side>(side_raw); }
    OrderType order_type() const { return static_cast<OrderType>(otype_raw); }

    static OrderCommand make_new(OrderId id,
                                 SymbolId sym,
                                 TraderId tid,
                                 Side s,
                                 Price p,
                                 Quantity q,
                                 Timestamp ts,
                                 OrderType t) {
        OrderCommand cmd;
        cmd.type = CommandType::NewOrder;
        cmd.side_raw = static_cast<uint8_t>(s);
        cmd.otype_raw = static_cast<uint8_t>(t);
        cmd.order_id = id;
        cmd.symbol_id = sym;
        cmd.trader_id = tid;
        cmd.price = p;
        cmd.quantity = q;
        cmd.timestamp = ts;
        return cmd;
    }

    static OrderCommand make_cancel(OrderId id) {
        OrderCommand cmd;
        cmd.type = CommandType::Cancel;
        cmd.order_id = id;
        return cmd;
    }

    static OrderCommand make_amend(OrderId id, Price np, Quantity nq) {
        OrderCommand cmd;
        cmd.type = CommandType::Amend;
        cmd.order_id = id;
        cmd.new_price = np;
        cmd.new_qty = nq;
        return cmd;
    }

    static OrderCommand make_shutdown() { return OrderCommand{}; }
};

static_assert(sizeof(OrderCommand) == 64, "OrderCommand must be exactly one cache line");