#include "order_book.hpp"

#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

void OrderBook::set_trade_callback(std::function<void(const TradeEvent &)> cb)
{
    on_trade_ = std::move(cb);
}

void OrderBook::set_order_callback(std::function<void(const OrderEvent &)> cb)
{
    on_order_event_ = std::move(cb);
}

void OrderBook::add_order(Order *order)
{
    if (!order)
        return;
    
    stats_.total_orders++;
    order_lookup_[order->order_id] = order;

    if (order->side == Side::Buy)
    {
        bids_[order->price].add_order(order);
    }
    else
    {
        asks_[order->price].add_order(order);
    }
    if (on_order_event_)
    {
        on_order_event_(OrderEvent{
            OrderEventType::New,
            order->order_id,
            order->symbol_id,
            order->side,
            order->price,
            order->quantity,
            0,
            order->quantity,
            get_timestamp_ns()
        });
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
                bids_.erase(bid_it);
        }
    }
    else
    {
        auto ask_it = asks_.find(order->price);
        if (ask_it != asks_.end())
        {
            ask_it->second.remove_order(order_id);

            if (ask_it->second.is_empty())
                asks_.erase(ask_it);
        }
    }

    if (on_order_event_)
    {
        on_order_event_(OrderEvent{
            OrderEventType::Cancelled,
            order->order_id,
            order->symbol_id,
            order->side,
            order->price,
            order->quantity,
            0,
            0,
            get_timestamp_ns()
        });
    }

    order_lookup_.erase(lookup_it);

    if (pool_.is_from_pool(order))
        pool_.deallocate(order);

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

std::vector<Trade> OrderBook::match(Order *order)
{
    std::vector<Trade> trades;
    match_impl(order, trades);
    return trades;
}

void OrderBook::match_impl(Order *order, std::vector<Trade> &trades)
{
    bool is_market = (order->order_type == OrderType::Market);
    bool is_ioc = (order->order_type == OrderType::IOC);
    bool is_fok = (order->order_type == OrderType::FOK);

    if (order->order_type != OrderType::Limit && !is_market && !is_ioc && !is_fok)
        return;

    if (is_fok && !can_fill(order))
    {
        if (on_order_event_)
        {
            on_order_event_(OrderEvent{
                OrderEventType::Cancelled,
                order->order_id,
                order->symbol_id,
                order->side,
                order->price,
                order->quantity,
                0,
                0,
                get_timestamp_ns()
            });
        }
        return;
    }

    Price original_price = order->price;
    if (is_market)
    {
        if (order->side == Side::Buy)
            order->price = std::numeric_limits<Price>::max();
        else
            order->price = 0;
    }

    bool is_buy = (order->side == Side::Buy);
    Quantity initial_qty = order->quantity;
    Quantity remaining_qty = order->quantity;

    if (is_buy)
    {
        auto it = asks_.begin();

        while (it != asks_.end() && remaining_qty > 0)
        {
            Price level_price = it->first;
            
            if (order->price < level_price)
                break;
            
            PriceLevel &level = it->second;

            while (!level.is_empty() && remaining_qty > 0)
            {
                Order *resting_order = level.front();

                if (UNLIKELY(resting_order->trader_id == order->trader_id))
                    break;
                
                Quantity resting_orig_qty = resting_order->quantity;
                Quantity trade_qty = std::min(remaining_qty, resting_order->quantity);

                Trade trade(
                    generate_trade_id(),
                    order->order_id,
                    resting_order->order_id,
                    level_price,
                    trade_qty,
                    get_timestamp_ns()
                );
                trades.push_back(trade);

                remaining_qty -= trade_qty;
                resting_order->quantity -= trade_qty;

                if (on_trade_)
                {
                    on_trade_(TradeEvent{
                        trade.trade_id,
                        trade.buy_order_id,
                        trade.sell_order_id,
                        trade.price,
                        trade.quantity,
                        trade.timestamp
                    });
                }

                if (on_order_event_)
                {
                    OrderEventType evt_type = (resting_order->quantity == 0) ? OrderEventType::Filled : OrderEventType::PartialFill;
                    on_order_event_(OrderEvent{
                        evt_type,
                        resting_order->order_id,
                        resting_order->symbol_id,
                        resting_order->side,
                        resting_order->price,
                        resting_orig_qty,
                        trade_qty,
                        resting_order->quantity,
                        get_timestamp_ns()
                    });
                }

                stats_.total_trades++;
                stats_.total_volume += trade_qty;

                if (resting_order->quantity == 0)
                {
                    order_lookup_.erase(resting_order->order_id);
                    level.remove_order(resting_order->order_id);
                    if (pool_.is_from_pool(resting_order))
                        pool_.deallocate(resting_order);
                }
            }
            if (level.is_empty())
                it = asks_.erase(it);
            else
                ++it;
        }
    }
    else
    {
        auto it = bids_.begin();

        while (it != bids_.end() && remaining_qty > 0)
        {
            Price level_price = it->first;
            
            if (order->price > level_price)
                break;

            PriceLevel &level = it->second;

            while (!level.is_empty() && remaining_qty > 0)
            {
                Order *resting_order = level.front();

                if (UNLIKELY(resting_order->trader_id == order->trader_id))
                    break;

                Quantity resting_orig_qty = resting_order->quantity;
                Quantity trade_qty = std::min(remaining_qty, resting_order->quantity);

                Trade trade(
                    generate_trade_id(),
                    resting_order->order_id,
                    order->order_id,
                    level_price,
                    trade_qty,
                    get_timestamp_ns()
                );
                trades.push_back(trade);

                remaining_qty -= trade_qty;
                resting_order->quantity -= trade_qty;

                if (on_trade_)
                {
                    on_trade_(TradeEvent{
                        trade.trade_id,
                        trade.buy_order_id,
                        trade.sell_order_id,
                        trade.price,
                        trade.quantity,
                        trade.timestamp
                    });
                }

                if (on_order_event_)
                {
                    OrderEventType evt_type = (resting_order->quantity == 0) ? OrderEventType::Filled : OrderEventType::PartialFill;
                    on_order_event_(OrderEvent{
                        evt_type,
                        resting_order->order_id,
                        resting_order->symbol_id,
                        resting_order->side,
                        resting_order->price,
                        resting_orig_qty,
                        trade_qty,
                        resting_order->quantity,
                        get_timestamp_ns()
                    });
                }

                stats_.total_trades++;
                stats_.total_volume += trade_qty;

                if (resting_order->quantity == 0)
                {
                    order_lookup_.erase(resting_order->order_id);
                    level.remove_order(resting_order->order_id);
                    if (pool_.is_from_pool(resting_order))
                        pool_.deallocate(resting_order);
                }
            }
            if (level.is_empty())
                it = bids_.erase(it);
            else
                ++it;
        }
    }

    order->quantity = remaining_qty;

    Quantity total_filled = initial_qty - remaining_qty;
    if (total_filled > 0 && on_order_event_)
    {
        OrderEventType evt_type = (remaining_qty == 0) ? OrderEventType::Filled : OrderEventType::PartialFill;
        on_order_event_(OrderEvent{
            evt_type,
            order->order_id,
            order->symbol_id,
            order->side,
            original_price,
            initial_qty,
            total_filled,
            remaining_qty,
            get_timestamp_ns()
        });
    }

    if (remaining_qty > 0)
    {
        if (is_market || is_ioc || is_fok)
        {
            order->price = original_price;
            if (on_order_event_)
            {
                on_order_event_(OrderEvent{
                    OrderEventType::Cancelled,
                    order->order_id,
                    order->symbol_id,
                    order->side,
                    order->price,
                    initial_qty,
                    total_filled,
                    0,
                    get_timestamp_ns()
                });
            }
        }
        else
        {
            add_order(order);
        }
    }
}

bool OrderBook::can_fill(const Order *order) const
{
    Quantity needed = order->quantity;

    if (order->side == Side::Buy)
    {
        for (const auto &[level_price, level] : asks_)
        {
            if (order->price < level_price)
                break;
            
            Quantity available = level.get_total_quantity();
            if (available >= needed)
                return true;
            needed -= available;
        }
    }
    else
    {
        for (const auto &[level_price, level] : bids_)
        {
            if (order->price > level_price)
                break;
            
            Quantity available = level.get_total_quantity();
            if (available >= needed)
                return true;
            needed -= available;
        }
    }

    return false;
}

bool OrderBook::amend_order(OrderId order_id, Quantity new_qty, Price new_price)
{
    auto it = order_lookup_.find(order_id);
    if (it == order_lookup_.end())
        return false;
    
    Order *order = it->second;

    if (new_qty == 0)
        return cancel_order(order_id);
    
    Price old_price = order->price;
    Quantity old_qty = order->quantity;

    bool loses_priority = (new_qty > old_qty) || (new_price != old_price);

    if (loses_priority)
    {
        if (order->side == Side::Buy)
        {
            auto bid_it = bids_.find(order->price);
            if (bid_it != bids_.end())
            {
                bid_it->second.remove_order(order_id);
                if (bid_it->second.is_empty())
                    bids_.erase(bid_it);
            }
        }
        else
        {
            auto ask_it = asks_.find(order->price);
            if (ask_it != asks_.end())
            {
                ask_it->second.remove_order(order_id);
                if (ask_it->second.is_empty())
                    asks_.erase(ask_it);
            }
        }

        order->price = new_price;
        order->quantity = new_qty;
        order->timestamp = get_timestamp_ns();

        if (order->side == Side::Buy)
            bids_[order->price].add_order(order);
        else
            asks_[order->price].add_order(order);
    }
    else
    {
        order->quantity = new_qty;
    }

    if (on_order_event_)
    {
        on_order_event_(OrderEvent{
            OrderEventType::Amended,
            order->order_id,
            order->symbol_id,
            order->side,
            new_price,
            new_qty,
            0,
            new_qty,
            get_timestamp_ns(),
            old_price,
            old_qty
        });
    }

    return true;
}

Order *OrderBook::create_order(OrderId id, SymbolId sym, TraderId trader, Side side, Price price, Quantity qty, Timestamp ts, OrderType type)
{
    Order *order = pool_.allocate(id, sym, trader, side, price, qty, ts, type);
    scratch_trades_.clear();
    match_impl(order, scratch_trades_);
    if (!has_order(id))
    {
        pool_.deallocate(order);
        return nullptr;
    }
    return order;
}