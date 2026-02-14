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

bool OrderBook::cancel_order(OrderId order_id)
{
    auto lookup_it = order_lookup_.find(order_id);
    if (lookup_it == order_lookup_.end())
        return false;
    
    Order *order = lookup_it->second;

    if (order->side == Side::Buy)
    {
        auto bid_it = bids_.find(order->price);
        if (bid_it != bids_.end())
        {
            bid_it->second.remove_order(order_id);

            if (bid_it->second.is_empty())
            {
                bids_.erase(bid_it);
            }
        }
    }
    else
    {
        auto ask_it = asks_.find(order->price);
        if (ask_it != asks_.end())
        {
            ask_it->second.remove_order(order_id);

            if (ask_it->second.is_empty())
            {
                asks_.erase(ask_it);
            }
        }
    }
    order_lookup_.erase(lookup_it);

    return true;
}

OrderBook::BBO OrderBook::get_best_bid() const
{
    if (bids_.empty())
        return {0, 0, false};
    
    const auto &best = bids_.begin();

    return
    {
        best->first,
        best->second.get_total_quantity(),
        true
    };
}

OrderBook::BBO OrderBook::get_best_ask() const
{
    if (asks_.empty())
        return {0, 0, false};

    const auto &best = asks_.begin();

    return
    {
        best->first,
        best->second.get_total_quantity(),
        true
    };
}

OrderBook::Spread OrderBook::get_spread() const
{
    auto best_bid = get_best_bid();
    auto best_ask = get_best_ask();

    if (!best_bid.valid || !best_ask.valid)
        return {0, false};

    return
    {
        best_ask.price - best_bid.price,
        true
    };
}