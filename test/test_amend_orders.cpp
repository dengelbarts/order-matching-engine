#include <gtest/gtest.h>
#include "../include/order_book.hpp"
#include <memory>
#include <vector>

class AmendOrderTest : public ::testing::Test
{
    protected:
        OrderBook book;
        std::vector<std::unique_ptr<Order>> orders;
        SymbolId symbol = 1;

        Order *create_order(Side side, Price price, Quantity qty, TraderId trader = 100, OrderType type = OrderType::Limit)
        {
            auto order = std::make_unique<Order>(
                generate_order_id(), symbol, trader, side, price, qty, get_timestamp_ns(), type
            );
            Order *ptr = order.get();
            orders.push_back(std::move(order));
            return ptr;
        }
};

TEST_F(AmendOrderTest, QtyDecreaseKeepsPriority)
{
    Order *o1 = create_order(Side::Buy, to_price(10.00), 100, 100);
    Order *o2 = create_order(Side::Buy, to_price(10.00), 100, 200);
    book.add_order(o1);
    book.add_order(o2);

    Timestamp ts_before = o1->timestamp;

    bool ok = book.amend_order(o1->order_id, 50, to_price(10.00));
    EXPECT_TRUE(ok);
    EXPECT_EQ(o1->quantity, 50);
    EXPECT_EQ(o1->timestamp, ts_before);
}

TEST_F(AmendOrderTest, QtyDecreaseReflectedInBBO)
{
    Order *o = create_order(Side::Buy, to_price(10.00), 100);
    book.add_order(o);

    book.amend_order(o->order_id, 60, to_price(10.00));
    EXPECT_EQ(book.get_best_bid().quantity, 60);
}

TEST_F(AmendOrderTest, PriceChangeLosesPriority)
{
    Order *o1 = create_order(Side::Buy, to_price(10.00), 100, 100);
    Order *o2 = create_order(Side::Buy, to_price(10.00), 100, 200);
    book.add_order(o1);
    book.add_order(o2);

    Timestamp ts_before = o1->timestamp;

    bool ok = book.amend_order(o1->order_id, 100, to_price(10.50));
    EXPECT_TRUE(ok);
    EXPECT_EQ(o1->price, to_price(10.50));
    EXPECT_GT(o1->timestamp, ts_before);
}

TEST_F(AmendOrderTest, PriceChangeMovesPriceLevel)
{
    Order *o = create_order(Side::Buy, to_price(10.00), 100);
    book.add_order(o);

    book.amend_order(o->order_id, 100, to_price(10.50));
    EXPECT_EQ(book.get_bids().count(to_price(10.00)), 0u);
    EXPECT_EQ(book.get_bids().count(to_price(10.50)), 1u);
}

TEST_F(AmendOrderTest, QtyIncreaseLosesPriority)
{
    Order *o1 = create_order(Side::Buy, to_price(10.00), 100, 100);
    Order *o2 = create_order(Side::Buy, to_price(10.00), 100, 200);
    book.add_order(o1);
    book.add_order(o2);

    Timestamp ts_before = o1->timestamp;
    book.amend_order(o1->order_id, 200, to_price(10.00));

    EXPECT_GT(o1->timestamp, ts_before);
}

TEST_F(AmendOrderTest, AmendNonExistentOrderReturnsFalse)
{
    bool ok = book.amend_order(99999, 100, to_price(10.00));
    EXPECT_FALSE(ok);
}

TEST_F(AmendOrderTest, AmendToZeroQtyActsAsCancel)
{
    Order *o = create_order(Side::Buy, to_price(10.00), 100);
    book.add_order(o);
    
    bool ok = book.amend_order(o->order_id, 0, to_price(10.00));
    EXPECT_TRUE(ok);
    EXPECT_FALSE(book.has_order(o->order_id));
    EXPECT_FALSE(book.get_best_bid().valid);
}

TEST_F(AmendOrderTest, AmendedEventFired)
{
    std::vector<OrderEvent> events;
    book.set_order_callback([&](const OrderEvent &e) { events.push_back(e); });
    Order *o = create_order(Side::Buy, to_price(10.00), 100);
    book.add_order(o);
    events.clear();

    book.amend_order(o->order_id, 50, to_price(10.00));

    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().type, OrderEventType::Amended);
    EXPECT_EQ(events.back().order_id, o->order_id);
    EXPECT_EQ(events.back().old_qty, 100u);
    EXPECT_EQ(events.back().original_qty, 50u);
}

TEST_F(AmendOrderTest, AmendedEventCarriesOldAndNewPrice)
{
    std::vector<OrderEvent> events;
    book.set_order_callback([&](const OrderEvent &e) { events.push_back(e); });

    Order *o = create_order(Side::Buy, to_price(10.00), 100);
    book.add_order(o);
    events.clear();

    book.amend_order(o->order_id, 100, to_price(11.00));

    ASSERT_FALSE(events.empty());
    const OrderEvent &ev = events.back();
    EXPECT_EQ(ev.type, OrderEventType::Amended);
    EXPECT_EQ(ev.old_price, to_price(10.00));
    EXPECT_EQ(ev.price, to_price(11.00));
}