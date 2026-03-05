#include "../include/order_book.hpp"
#include "../include/trade.hpp"

#include <memory>
#include <vector>

#include <gtest/gtest.h>

class MarketOrderTest : public ::testing::Test {
protected:
    OrderBook book;
    std::vector<std::unique_ptr<Order>> orders;
    SymbolId symbol = 1;

    Order* create_order(Side side,
                        Price price,
                        Quantity qty,
                        TraderId trader = 100,
                        OrderType type = OrderType::Limit) {
        auto order = std::make_unique<Order>(
            generate_order_id(), symbol, trader, side, price, qty, get_timestamp_ns(), type);
        Order* ptr = order.get();
        orders.push_back(std::move(order));
        return ptr;
    }

    Order* create_market(Side side, Quantity qty, TraderId trader = 100) {
        return create_order(side, 0, qty, trader, OrderType::Market);
    }
};

TEST_F(MarketOrderTest, MarketBuyFullFill) {
    Order* sell = create_order(Side::Sell, to_price(10.00), 100, 200);
    book.add_order(sell);

    Order* buy = create_market(Side::Buy, 100);
    auto trades = book.match(buy);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 100);
    EXPECT_EQ(trades[0].price, to_price(10.00));

    EXPECT_FALSE(book.has_order(sell->order_id));
    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.get_best_ask().valid);
}

TEST_F(MarketOrderTest, MarketBuyMultiLevel) {
    Order* sell1 = create_order(Side::Sell, to_price(10.00), 50, 200);
    Order* sell2 = create_order(Side::Sell, to_price(11.00), 50, 300);
    book.add_order(sell1);
    book.add_order(sell2);

    Order* buy = create_market(Side::Buy, 100);
    auto trades = book.match(buy);

    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].price, to_price(10.00));
    EXPECT_EQ(trades[0].quantity, 50);
    EXPECT_EQ(trades[1].price, to_price(11.00));
    EXPECT_EQ(trades[1].quantity, 50);

    EXPECT_FALSE(book.has_order(sell1->order_id));
    EXPECT_FALSE(book.has_order(sell2->order_id));
    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.get_best_ask().valid);
}

TEST_F(MarketOrderTest, MarketBuyPartialFillRemainderCancelled) {
    Order* sell = create_order(Side::Sell, to_price(10.00), 50, 200);
    book.add_order(sell);

    Order* buy = create_market(Side::Buy, 100);
    auto trades = book.match(buy);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 50);

    EXPECT_FALSE(book.has_order(sell->order_id));
    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.get_best_bid().valid);
}

TEST_F(MarketOrderTest, MarketBuyEmptyBook) {
    Order* buy = create_market(Side::Buy, 100);
    auto trades = book.match(buy);

    EXPECT_EQ(trades.size(), 0);
    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.get_best_bid().valid);
}

TEST_F(MarketOrderTest, MarketSellFullFill) {
    Order* buy = create_order(Side::Buy, to_price(10.00), 100, 200);
    book.add_order(buy);

    Order* sell = create_market(Side::Sell, 100);
    auto trades = book.match(sell);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 100);
    EXPECT_EQ(trades[0].price, to_price(10.00));

    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.has_order(sell->order_id));
    EXPECT_FALSE(book.get_best_bid().valid);
}

TEST_F(MarketOrderTest, MarketSellPartialFillRemainderCancelled) {
    Order* buy = create_order(Side::Buy, to_price(10.00), 50, 200);
    book.add_order(buy);

    Order* sell = create_market(Side::Sell, 100);
    auto trades = book.match(sell);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 50);

    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.has_order(sell->order_id));
    EXPECT_FALSE(book.get_best_ask().valid);
}

TEST_F(MarketOrderTest, MarketSellEmptyBook) {
    Order* sell = create_market(Side::Sell, 100);
    auto trades = book.match(sell);

    EXPECT_EQ(trades.size(), 0);
    EXPECT_FALSE(book.has_order(sell->order_id));
    EXPECT_FALSE(book.get_best_ask().valid);
}

TEST_F(MarketOrderTest, MarketOrderFiresCancelEventOnEmptyBook) {
    std::vector<OrderEvent> events;
    book.set_order_callback([&](const OrderEvent& e) { events.push_back(e); });

    Order* buy = create_market(Side::Buy, 100);
    book.match(buy);

    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().type, OrderEventType::Cancelled);
    EXPECT_EQ(events.back().order_id, buy->order_id);
    EXPECT_EQ(events.back().remaining_qty, 0);
}