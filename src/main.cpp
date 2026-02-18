#include <iostream>
#include <memory>
#include <vector>
#include "order_book.hpp"

int main()
{
    std::cout << "=== Order Matching Engine v0.1.0 - Phase 1 Demo ===\n\n";

    OrderBook book;
    book.set_trade_callback([](const TradeEvent &te)
    {
        std::cout << "[TRADE] " << te << "\n";
    });

    book.set_order_callback([](const OrderEvent &oe)
    {
        std::cout << "[ORDER] " << oe << "\n";
    });

    std::vector<std::unique_ptr<Order>> orders;

    auto make_order = [&](Side side, double price, Quantity qty, TraderId trader = 1, OrderType type = OrderType::Limit) -> Order*
    {
        orders.push_back(std::make_unique<Order>(generate_order_id(), 1u, trader, side, to_price(price), qty, get_timestamp_ns(), type));
        return orders.back().get();
    };

    std::cout << "--- Adding resting orders ---\n";
    book.add_order(make_order(Side::Sell, 10.50, 100, 10));
    book.add_order(make_order(Side::Sell, 10.25, 75, 20));
    book.add_order(make_order(Side::Sell, 10.00, 50, 30));
    book.add_order(make_order(Side::Buy, 9.75, 80, 40));
    book.add_order(make_order(Side::Buy, 9.50, 60, 50));

    auto bid = book.get_best_bid();
    auto ask = book.get_best_ask();
    auto spread = book.get_spread();

    std::cout << "\nBBO: bid " << price_to_string(bid.price)
                << " x " << bid.quantity
                << " | ask " << price_to_string(ask.price)
                << " x " << ask.quantity << "\n";
    std::cout << "Spread: " << price_to_string(spread.value) << "\n\n";

    std::cout << "--- Aggressive buy (qty 120 @ 10.50) <<<\n";
    auto *agg_buy = make_order(Side::Buy, 10.50, 120, 99);
    book.match(agg_buy);

    std::cout << "\nAfter sweep:\n";
    auto bid2 = book.get_best_bid();
    auto ask2 = book.get_best_ask();
    std::cout << "Best bid: ";
    if (bid2.valid) std::cout << price_to_string(bid2.price) << " x " << bid2.quantity << "\n";
    else std::cout << "(none)\n";
    std::cout << "Best ask: ";
    if (ask2.valid) std::cout << price_to_string(ask2.price) << " x " << ask2.quantity << "\n";
    else std::cout << "(none)\n";

    const auto &stats = book.get_stats();
    std::cout << "\n--- Stats ---\n";
    std::cout << "Total orders added to book : " << stats.total_orders << "\n";
    std::cout << "Total trades executed      : " << stats.total_trades << "\n";
    std::cout << "Total volume traded        : " << stats.total_volume << "\n";

    return 0;
}