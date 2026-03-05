#pragma once

#include "order.hpp"
#include "trade.hpp"

#include <ostream>

enum class OrderEventType { New, Cancelled, Filled, PartialFill, Amended };

inline const char* to_string(OrderEventType type) {
    switch (type) {
    case OrderEventType::New:
        return "New";
    case OrderEventType::Cancelled:
        return "Cancelled";
    case OrderEventType::Filled:
        return "Filled";
    case OrderEventType::Amended:
        return "Amended";
    case OrderEventType::PartialFill:
        return "PartialFill";
    }
    return "Unknown";
}

struct OrderEvent {
    OrderEventType type;
    OrderId order_id;
    SymbolId symbol_id;
    Side side;
    Price price;
    Quantity original_qty;
    Quantity filled_qty;
    Quantity remaining_qty;
    Timestamp timestamp;
    Price old_price = 0;
    Quantity old_qty = 0;
};

struct TradeEvent {
    TradeId trade_id;
    OrderId buy_order_id;
    OrderId sell_order_id;
    Price price;
    Quantity quantity;
    Timestamp timestamp;
};

inline std::ostream& operator<<(std::ostream& os, const OrderEvent& e) {
    os << "OrderEvent{"
       << "type=" << to_string(e.type) << ", id=" << e.order_id << ", side=" << to_string(e.side)
       << ", price=" << price_to_string(e.price) << ", orig=" << e.original_qty
       << ", filled=" << e.filled_qty << ", rem=" << e.remaining_qty << "}";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const TradeEvent& e) {
    os << "TradeEvent{"
       << "id=" << e.trade_id << ", buy=" << e.buy_order_id << ", sell=" << e.sell_order_id
       << ", price=" << price_to_string(e.price) << ", qty=" << e.quantity << "}";
    return os;
}