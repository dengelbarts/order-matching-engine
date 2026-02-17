#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <algorithm>
#include "../include/order_book.hpp"
#include "../include/events.hpp"

class EventTest : public ::testing::Test
{
    private:
        std::vector<std::unique_ptr<Order>> owned_;

    protected:
        OrderBook book;
        std::vector<TradeEvent> trade_events;
        std::vector<OrderEvent> order_events;
        SymbolId symbol = 1;

        void SetUp() override
        {
            book.set_trade_callback([this](const TradeEvent &te)
            {
                trade_events.push_back(te);
            });
            book.set_order_callback([this](const OrderEvent &oe)
            {
                order_events.push_back(oe);
            });
        }

        Order *create_order(Side side, Price price, Quantity qty, TraderId trader = 100, OrderType type = OrderType::Limit)
        {
            owned_.push_back(std::make_unique<Order>(generate_order_id(), symbol, trader, side, price, qty, get_timestamp_ns(), type));
            return owned_.back().get();
        }

        size_t count_event_type(OrderEventType t) const
        {
            return static_cast<size_t>(std::count_if(order_events.begin(), order_events.end(), [t](const OrderEvent &e) {return e.type == t; }));
        }
};

TEST_F(EventTest, AddOrderFiresNewEvent)
{
    Order *buy = create_order(Side::Buy, to_price(10.00), 100);
    book.add_order(buy);

    ASSERT_EQ(order_events.size(), 1u);
    EXPECT_EQ(order_events[0].type, OrderEventType::New);
    EXPECT_EQ(order_events[0].order_id, buy->order_id);
    EXPECT_EQ(order_events[0].side, Side::Buy);
    EXPECT_EQ(order_events[0].price, to_price(10.00));
    EXPECT_EQ(order_events[0].original_qty, 100u);
    EXPECT_EQ(order_events[0].remaining_qty, 100u);
    EXPECT_EQ(order_events[0].filled_qty, 0u);
    EXPECT_EQ(trade_events.size(), 0u);
}

TEST_F(EventTest, CancelOrderFiresCancelledEvent)
{
    Order *buy = create_order(Side::Buy, to_price(10.00), 100);
    book.add_order(buy);
    order_events.clear();

    book.cancel_order(buy->order_id);

    ASSERT_EQ(order_events.size(), 1u);
    EXPECT_EQ(order_events[0].type, OrderEventType::Cancelled);
    EXPECT_EQ(order_events[0].order_id, buy->order_id);
}

TEST_F(EventTest, ExactMatchFiresTradeAndFillEvents)
{
    Order *sell = create_order(Side::Sell, to_price(10.00), 100, 200);
    book.add_order(sell);
    order_events.clear();

    Order *buy = create_order(Side::Buy, to_price(10.00), 100, 100);
    book.match(buy);

    ASSERT_EQ(trade_events.size(), 1u);
    EXPECT_EQ(trade_events[0].price, to_price(10.00));
    EXPECT_EQ(trade_events[0].quantity, 100u);
    EXPECT_EQ(trade_events[0].buy_order_id, buy->order_id);
    EXPECT_EQ(trade_events[0].sell_order_id, sell->order_id);

    ASSERT_EQ(order_events.size(), 2u);
    EXPECT_EQ(count_event_type(OrderEventType::Filled), 2u);
}

TEST_F(EventTest, PartialFillIncomingOrderEvents)
{
    Order *sell = create_order(Side::Sell, to_price(10.00), 50, 200);
    book.add_order(sell);
    order_events.clear();

    Order *buy = create_order(Side::Buy, to_price(10.00), 100, 100);
    book.match(buy);

    ASSERT_EQ(trade_events.size(), 1u);
    EXPECT_EQ(trade_events[0].quantity, 50u);

    EXPECT_EQ(count_event_type(OrderEventType::Filled), 1u);
    EXPECT_EQ(count_event_type(OrderEventType::PartialFill), 1u);
    EXPECT_EQ(count_event_type(OrderEventType::New), 1u);
}

TEST_F(EventTest, NoMatchOnlyNewEvent)
{
    Order *sell = create_order(Side::Sell, to_price(11.00), 100, 200);
    book.add_order(sell);
    order_events.clear();

    Order *buy = create_order(Side::Buy, to_price(9.00), 100, 100);
    book.match(buy);

    EXPECT_EQ(trade_events.size(), 0u);

    ASSERT_EQ(order_events.size(), 1u);
    EXPECT_EQ(order_events[0].type, OrderEventType::New);
}

TEST_F(EventTest, StatsTracking)
{
    Order *buy1 = create_order(Side::Buy, to_price(10.00), 100, 100);
    Order *buy2 = create_order(Side::Buy, to_price(10.00), 100, 100);
    book.add_order(buy1);
    book.add_order(buy2);

    EXPECT_EQ(book.get_stats().total_orders, 2u);
    EXPECT_EQ(book.get_stats().total_trades, 0u);
    EXPECT_EQ(book.get_stats().total_volume, 0u);

    Order *sell = create_order(Side::Sell, to_price(10.00), 250, 200);
    book.match(sell);

    EXPECT_EQ(book.get_stats().total_trades, 2u);
    EXPECT_EQ(book.get_stats().total_volume, 200u);
    EXPECT_EQ(book.get_stats().total_orders, 3u);
}

TEST_F(EventTest, EventOrderAddMatchTrade)
{
    Order *sell = create_order(Side::Sell, to_price(10.00), 100, 200);
    book.add_order(sell);
    order_events.clear();

    Order *buy = create_order(Side::Buy, to_price(10.00), 100, 100);
    book.match(buy);

    ASSERT_EQ(trade_events.size(), 1u);

    ASSERT_EQ(order_events.size(), 2u);
    EXPECT_EQ(order_events[0].order_id, sell->order_id);
    EXPECT_EQ(order_events[1].order_id, buy->order_id);
}

TEST_F(EventTest, IntegrationTest220Orders)
{
    const Price P1000 = to_price(10.00);
    const Price P1100 = to_price(11.00);

    std::vector<std::unique_ptr<Order>> buys;
    for (int i = 0; i < 10; ++i)
    {
        buys.push_back(std::make_unique<Order>(generate_order_id(), symbol, 1, Side::Buy, P1000, 100, get_timestamp_ns(), OrderType::Limit));
        book.add_order(buys.back().get());
    }
    EXPECT_EQ(count_event_type(OrderEventType::New), 10u);
    EXPECT_EQ(book.get_stats().total_orders, 10u);

    std::vector<std::unique_ptr<Order>> sells;
    for (int i = 0; i < 10; ++i)
    {
        sells.push_back(std::make_unique<Order>(generate_order_id(), symbol, 2, Side::Sell, P1100, 100, get_timestamp_ns(), OrderType::Limit));
        book.add_order(sells.back().get());
    }
    EXPECT_EQ(count_event_type(OrderEventType::New), 20u);
    EXPECT_EQ(book.get_stats().total_orders, 20u);
    EXPECT_EQ(trade_events.size(), 0u);

    for (int i = 0; i < 5; ++i)
        book.cancel_order(buys[i]->order_id);

    EXPECT_EQ(count_event_type(OrderEventType::Cancelled), 5u);

    Order agg_sell(generate_order_id(), symbol, 3, Side::Sell, P1000, 500, get_timestamp_ns(), OrderType::Limit);
    book.match(&agg_sell);

    EXPECT_EQ(trade_events.size(), 5u);
    EXPECT_EQ(book.get_stats().total_trades, 5u);
    EXPECT_EQ(book.get_stats().total_volume, 500u);
    EXPECT_EQ(count_event_type(OrderEventType::Filled), 6u);
    EXPECT_EQ(book.get_stats().total_orders, 20u);

    Order agg_buy(generate_order_id(), symbol, 4, Side::Buy, P1100, 1000, get_timestamp_ns(), OrderType::Limit);
    book.match(&agg_buy);

    EXPECT_EQ(trade_events.size(), 15u);
    EXPECT_EQ(book.get_stats().total_trades, 15u);
    EXPECT_EQ(book.get_stats().total_volume, 1500u);
    EXPECT_EQ(count_event_type(OrderEventType::Filled), 17u);
    EXPECT_EQ(count_event_type(OrderEventType::PartialFill), 0u);
    EXPECT_EQ(count_event_type(OrderEventType::New), 20u);
    EXPECT_EQ(count_event_type(OrderEventType::Cancelled), 5u);

    EXPECT_FALSE(book.get_best_bid().valid);
    EXPECT_FALSE(book.get_best_ask().valid);
}