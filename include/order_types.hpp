#pragma once

enum class Side { Buy, Sell };

enum class OrderType { Limit, Market, IOC, FOK };

inline const char* to_string(Side side) {
    return side == Side::Buy ? "Buy" : "Sell";
}

inline const char* to_string(OrderType type) {
    switch (type) {
    case OrderType::Limit:
        return "Limit";
    case OrderType::Market:
        return "Market";
    case OrderType::IOC:
        return "IOC";
    case OrderType::FOK:
        return "FOK";
    }
    return "Unknown";
}