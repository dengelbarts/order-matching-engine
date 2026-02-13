#include <gtest/gtest.h>
#include "order_book.hpp"

class OrderBookTest : public ::testing::Test
{
    protected:
        OrderBook book;
        std::vector<std::unique_ptr<Order>> orders;

        Order *create_order(Side side, Price price, Quantity qty)
        {
            auto order = std::make_unique<Order>
            (
                generate_order_id(),
                1, // symbol_id
                side,
                price,
                qty,
                get_timestamp_ns(),
                OrderType::Limit
            );
            Order *ptr = order.get();
            orders.push_back(std::move(order));
            return ptr;
        }
};

TEST_F(OrderBookTest, AddSingleBuyOrder)
{
    Order *order = create_order(Side::Buy, to_price(10.50), 100);
    book.add_order(order);

    EXPECT_TRUE(book.has_order(order->order_id));
    EXPECT_EQ(book.get_order(order->order_id), order);

    const auto &bids = book.get_bids();
    EXPECT_EQ(bids.size(), 1);
    EXPECT_TRUE(bids.find(to_price(10.50)) != bids.end());
}

TEST_F(OrderBookTest, AddSingleSellOrder)
{
    Order *order = create_order(Side::Sell, to_price(10.75), 50);
    book.add_order(order);

    EXPECT_TRUE(book.has_order(order->order_id));

    const auto &asks = book.get_asks();
    EXPECT_EQ(asks.size(), 1);
    EXPECT_TRUE(asks.find(to_price(10.75)) != asks.end());
}

TEST_F(OrderBookTest, AddMultipleOrdersAtSamePrice)
{
    Order *order1 = create_order(Side::Buy, to_price(10.00), 100);
    Order *order2 = create_order(Side::Buy, to_price(10.00), 200);
    Order *order3 = create_order(Side::Buy, to_price(10.00), 150);

    book.add_order(order1);
    book.add_order(order2);
    book.add_order(order3);

    const auto &bids = book.get_bids();
    EXPECT_EQ(bids.size(), 1);

    auto it = bids.find(to_price(10.00));
    ASSERT_TRUE(it != bids.end());

    const PriceLevel &level = it->second;
    EXPECT_EQ(level.order_count(), 3);
    EXPECT_EQ(level.get_total_quantity(), 450);
}

TEST_F(OrderBookTest, AddOrdersOnBothSides)
{
    Order *buy1 = create_order(Side::Buy, to_price(10.00), 100);
    Order *buy2 = create_order(Side::Buy, to_price(9.50), 200);
    Order *sell1 = create_order(Side::Sell, to_price(10.50), 150);
    Order *sell2 = create_order(Side::Sell, to_price(11.00), 75);

    book.add_order(buy1);
    book.add_order(buy2);
    book.add_order(sell1);
    book.add_order(sell2);

    const auto &bids = book.get_bids();
    const auto &asks = book.get_asks();

    EXPECT_EQ(bids.size(), 2);
    EXPECT_EQ(asks.size(), 2);
    
    EXPECT_TRUE(book.has_order(buy1->order_id));
    EXPECT_TRUE(book.has_order(buy2->order_id));
    EXPECT_TRUE(book.has_order(sell1->order_id));
    EXPECT_TRUE(book.has_order(sell2->order_id));
}

TEST_F(OrderBookTest, BidsSortedDescending)
{
    Order *order1 = create_order(Side::Buy, to_price(10.00), 100);
    Order *order2 = create_order(Side::Buy, to_price(10.50), 100);
    Order *order3 = create_order(Side::Buy, to_price(9.50), 100);

    book.add_order(order1);
    book.add_order(order2);
    book.add_order(order3);

    const auto &bids = book.get_bids();
    auto it = bids.begin();

    EXPECT_EQ(it->first, to_price(10.50));
    ++it;
    EXPECT_EQ(it->first, to_price(10.00));
    ++it;
    EXPECT_EQ(it->first, to_price(9.50));
}

TEST_F(OrderBookTest, AsksSortedAscending)
{
    Order *order1 = create_order(Side::Sell, to_price(11.00), 100);
    Order *order2 = create_order(Side::Sell, to_price(10.50), 100);
    Order *order3 = create_order(Side::Sell, to_price(11.50), 100);

    book.add_order(order1);
    book.add_order(order2);
    book.add_order(order3);

    const auto &asks = book.get_asks();
    auto it = asks.begin();

    EXPECT_EQ(it->first, to_price(10.50));
    ++it;
    EXPECT_EQ(it->first, to_price(11.00));
    ++it;
    EXPECT_EQ(it->first, to_price(11.50));
}

TEST_F(OrderBookTest, OrderBookRetrievalById)
{
    Order *order = create_order(Side::Buy, to_price(10.00), 100);
    book.add_order(order);

    Order *retrieved = book.get_order(order->order_id);
    EXPECT_EQ(retrieved, order);
    EXPECT_EQ(retrieved->order_id, order->order_id);
    EXPECT_EQ(retrieved->price, to_price(10.00));
    EXPECT_EQ(retrieved->quantity, 100);
}

TEST_F(OrderBookTest, NonExistentOrderLookup)
{
    EXPECT_FALSE(book.has_order(999999));
    EXPECT_EQ(book.get_order(999999), nullptr);
}