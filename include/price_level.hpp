#pragma once

#include "order.hpp"

#include <cstddef>
#include <deque>

class PriceLevel {
private:
    std::deque<Order*> orders_;

public:
    void add_order(Order* order);
    bool remove_order(OrderId order_id);
    Quantity get_total_quantity() const;
    Order* front() const;
    bool is_empty() const;
    std::size_t order_count() const;
};