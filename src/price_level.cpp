#include "price_level.hpp"

#include <algorithm>

void PriceLevel::add_order(Order* order) {
    orders_.push_back(order);
}

bool PriceLevel::remove_order(OrderId order_id) {
    auto it = std::find_if(orders_.begin(), orders_.end(), [order_id](const Order* o) {
        return o->order_id == order_id;
    });

    if (it != orders_.end()) {
        orders_.erase(it);
        return true;
    }
    return false;
}

Quantity PriceLevel::get_total_quantity() const {
    Quantity total = 0;
    for (const auto* order : orders_) {
        total += order->quantity;
    }
    return total;
}

Order* PriceLevel::front() const {
    if (orders_.empty()) {
        return nullptr;
    }
    return orders_.front();
}

bool PriceLevel::is_empty() const {
    return orders_.empty();
}

std::size_t PriceLevel::order_count() const {
    return orders_.size();
}