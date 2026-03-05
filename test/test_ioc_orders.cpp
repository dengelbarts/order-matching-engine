#include "../include/order_book.hpp"
#include "../include/trade.hpp"

#include <memory>
#include <vector>

#include <gtest/gtest.h>

class IOCOrderTest : public ::testing::Test {
protected:
    OrderBook book;
    std::vector<std::unique_ptr<Order>> orders;
    SymbolId symbol = 1;

    Order* create_order(Side side,
                        Price price,
                        Quantity qty,
                        TraderId trader = 100,
                        OrderType type = OrderType::Limit)

    {
        auto order = std::make_unique<Order>(
            generate_order_id(), symbol, trader, side, price, qty, get_timestamp_ns(), type);
        Order* ptr = order.get();
        orders.push_back(std::move(order));
        return ptr;
    }

    Order* create_ioc(Side side, Price price, Quantity qty, TraderId trader = 100) {
        return create_order(side, price, qty, trader, OrderType::IOC);
    }
};

TEST_F(IOCOrderTest, IOCBuyFullFill) {
    Order* sell = create_order(Side::Sell, to_price(10.00), 100, 200);
    book.add_order(sell);

    Order* buy = create_ioc(Side::Buy, to_price(10.50), 100);
    auto trades = book.match(buy);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 100);
    EXPECT_EQ(trades[0].price, to_price(10.00));

    EXPECT_FALSE(book.has_order(sell->order_id));
    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.get_best_ask().valid);
}

TEST_F(IOCOrderTest, IOCBuyPartialFill) {
    Order* sell = create_order(Side::Sell, to_price(10.00), 50, 200);
    book.add_order(sell);

    Order* buy = create_ioc(Side::Buy, to_price(10.50), 100);
    auto trades = book.match(buy);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 50);

    EXPECT_FALSE(book.has_order(sell->order_id));
    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.get_best_ask().valid);
}

TEST_F(IOCOrderTest, IOCBuyNoFillPriceMismatch) {
    Order* sell = create_order(Side::Sell, to_price(10.00), 50, 200);
    book.add_order(sell);

    Order* buy = create_ioc(Side::Buy, to_price(9.50), 100);
    auto trades = book.match(buy);

    EXPECT_EQ(trades.size(), 0);
    EXPECT_TRUE(book.has_order(sell->order_id));

    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.get_best_bid().valid);
}

TEST_F(IOCOrderTest, IOCBuyEmptyBook) {
    Order* buy = create_ioc(Side::Buy, to_price(10.00), 100);
    auto trades = book.match(buy);

    EXPECT_EQ(trades.size(), 0);
    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.get_best_bid().valid);
}

TEST_F(IOCOrderTest, IOCBuyRespectsLimitPrice) {
    Order* sell1 = create_order(Side::Sell, to_price(10.00), 50, 200);
    Order* sell2 = create_order(Side::Sell, to_price(11.00), 50, 300);
    book.add_order(sell1);
    book.add_order(sell2);

    Order* buy = create_ioc(Side::Buy, to_price(10.50), 100);
    auto trades = book.match(buy);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].price, to_price(10.00));
    EXPECT_EQ(trades[0].quantity, 50);

    EXPECT_TRUE(book.has_order(sell2->order_id));
    EXPECT_FALSE(book.has_order(buy->order_id));
}

TEST_F(IOCOrderTest, IOCSellFullFill) {
    Order* buy = create_order(Side::Buy, to_price(10.00), 100, 200);
    book.add_order(buy);

    Order* sell = create_ioc(Side::Sell, to_price(9.50), 100);
    auto trades = book.match(sell);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 100);
    EXPECT_EQ(trades[0].price, to_price(10.00));

    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.has_order(sell->order_id));
    EXPECT_FALSE(book.get_best_bid().valid);
}

TEST_F(IOCOrderTest, IOCSellPartialFill) {
    Order* buy = create_order(Side::Buy, to_price(10.00), 50, 200);
    book.add_order(buy);

    Order* sell = create_ioc(Side::Sell, to_price(9.50), 100);
    auto trades = book.match(sell);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 50);

    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.has_order(sell->order_id));
    EXPECT_FALSE(book.get_best_bid().valid);
}

TEST_F(IOCOrderTest, IOCFiresCancelEventOnNoFill) {
    std::vector<OrderEvent> events;
    book.set_order_callback([&](const OrderEvent& e) { events.push_back(e); });

    Order* buy = create_ioc(Side::Buy, to_price(9.00), 100);
    book.match(buy);

    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().type, OrderEventType::Cancelled);
    EXPECT_EQ(events.back().order_id, buy->order_id);
    EXPECT_EQ(events.back().remaining_qty, 0);
}

TEST_F(IOCOrderTest, IOCFiresPartialFillThenCancelEvents) {
    std::vector<OrderEvent> events;
    book.set_order_callback([&](const OrderEvent& e) { events.push_back(e); });

    Order* sell = create_order(Side::Sell, to_price(10.00), 50, 200);
    book.add_order(sell);

    Order* buy = create_ioc(Side::Buy, to_price(10.50), 100);
    book.match(buy);

    std::vector<OrderEvent> ioc_events;
    for (const auto& e : events)
        if (e.order_id == buy->order_id)
            ioc_events.push_back(e);

    ASSERT_EQ(ioc_events.size(), 2);
    EXPECT_EQ(ioc_events[0].type, OrderEventType::PartialFill);
    EXPECT_EQ(ioc_events[0].filled_qty, 50);
    EXPECT_EQ(ioc_events[1].type, OrderEventType::Cancelled);
    EXPECT_EQ(ioc_events[1].remaining_qty, 0);
}