#include "order_book.hpp"

void OrderBook::add_order(Order *order)
{
    if (!order)
        return;
    
    order_lookup_[order->order_id] = order;

    if (order->side == Side::Buy)
    {
        bids_[order->price].add_order(order);
    }
    else
    {
        asks_[order->price].add_order(order);
    }
}

bool OrderBook::has_order(OrderId order_id) const
{
    return order_lookup_.find(order_id) != order_lookup_.end();
}

Order *OrderBook::get_order(OrderId order_id) const
{
    auto it = order_lookup_.find(order_id);
    if (it != order_lookup_.end())
        return it ->second;
    return nullptr;
}
