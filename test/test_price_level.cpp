#include <gtest/gtest.h>
#include "price_level.hpp"

TEST(PriceLevelTest, EmptyLevel)
{
    PriceLevel level;
    EXPECT_TRUE(level.is_empty());
    EXPECT_EQ(level.order_count(), 0);
    EXPECT_EQ(level.get_total_quantity(), 0);
    EXPECT_EQ(level.front(), nullptr);
}

TEST(PriceLevelTest, AddSingleOrder)
{
    PriceLevel level;
    Order order(1, 100, 100, Side::Buy, to_price(10.00), 50, 1000, OrderType::Limit);

    level.add_order(&order);

    EXPECT_FALSE(level.is_empty());
    EXPECT_EQ(level.order_count(), 1);
    EXPECT_EQ(level.get_total_quantity(), 50);
    EXPECT_EQ(level.front(), &order);
}

TEST(PriceLevelTest, FIFOOrdering)
{
    PriceLevel level;
    Order order1(1, 100, 100, Side::Buy, to_price(10.00), 50, 1000, OrderType::Limit);
    Order order2(2, 100, 100, Side::Buy, to_price(10.00), 60, 2000, OrderType::Limit);
    Order order3(3, 100, 100, Side::Buy, to_price(10.00), 70, 3000, OrderType::Limit);

    level.add_order(&order1);
    level.add_order(&order2);
    level.add_order(&order3);

    EXPECT_EQ(level.front(), &order1);
    EXPECT_EQ(level.order_count(), 3);
}

TEST(PriceLevelTest, TotalQuantity)
{
    PriceLevel level;

    Order order1(1, 100, 100, Side::Buy, to_price(10.00), 50, 1000, OrderType::Limit);
    Order order2(2, 100, 100, Side::Buy, to_price(10.00), 60, 2000, OrderType::Limit);
    Order order3(3, 100, 100, Side::Buy, to_price(10.00), 70, 3000, OrderType::Limit);

    level.add_order(&order1);
    level.add_order(&order2);
    level.add_order(&order3);

    EXPECT_EQ(level.get_total_quantity(), 180);
}

TEST(PriceLevelTest, RemoveMiddleOrder)
{
    PriceLevel level;

    Order order1(1, 100, 100, Side::Buy, to_price(10.00), 50, 1000, OrderType::Limit);
    Order order2(2, 100, 100, Side::Buy, to_price(10.00), 60, 2000, OrderType::Limit);
    Order order3(3, 100, 100, Side::Buy, to_price(10.00), 70, 3000, OrderType::Limit);

    level.add_order(&order1);
    level.add_order(&order2);
    level.add_order(&order3);

    bool removed = level.remove_order(2);

    EXPECT_TRUE(removed);
    EXPECT_EQ(level.order_count(), 2);
    EXPECT_EQ(level.get_total_quantity(), 120);
    EXPECT_EQ(level.front(), &order1);
}

TEST(PriceLevelTest, RemoveFirstOrder)
{
    PriceLevel level;

    Order order1(1, 100, 100, Side::Buy, to_price(10.00), 50, 1000, OrderType::Limit);
    Order order2(2, 100, 100, Side::Buy, to_price(10.00), 60, 2000, OrderType::Limit);

    level.add_order(&order1);
    level.add_order(&order2);

    bool removed = level.remove_order(1);

    EXPECT_TRUE(removed);
    EXPECT_EQ(level.order_count(), 1);
    EXPECT_EQ(level.front(), &order2);
}

TEST(PriceLevelTest, RemoveNonExistentOrder)
{
    PriceLevel level;

    Order order1(1, 100, 100, Side::Buy, to_price(10.00), 50, 1000, OrderType::Limit);

    level.add_order(&order1);

    bool removed = level.remove_order(999);

    EXPECT_FALSE(removed);
    EXPECT_EQ(level.order_count(), 1);
}

TEST(PriceLevelTest, RemoveFromEmptyLevel)
{
    PriceLevel level;

    bool removed = level.remove_order(1);

    EXPECT_FALSE(removed);
    EXPECT_TRUE(level.is_empty());
}

TEST(PriceLevelTest, RemoveAllOrders)
{
    PriceLevel level;

    Order order1(1, 100, 100, Side::Buy, to_price(10.00), 50, 1000, OrderType::Limit);
    Order order2(2, 100, 100, Side::Buy, to_price(10.00), 60, 2000, OrderType::Limit);

    level.add_order(&order1);
    level.add_order(&order2);

    level.remove_order(1);
    level.remove_order(2);

    EXPECT_TRUE(level.is_empty());
    EXPECT_EQ(level.order_count(), 0);
    EXPECT_EQ(level.get_total_quantity(), 0);
    EXPECT_EQ(level.front(), nullptr);
}

TEST(PriceLevelTest, AddAfterRemoveMaintainsFIFO)
{
    PriceLevel level;

    Order order1(1, 100, 100, Side::Buy, to_price(10.00), 50, 1000, OrderType::Limit);
    Order order2(2, 100, 100, Side::Buy, to_price(10.00), 60, 2000, OrderType::Limit);
    Order order3(3, 100, 100, Side::Buy, to_price(10.00), 70, 3000, OrderType::Limit);

    level.add_order(&order1);
    level.add_order(&order2);
    level.remove_order(1);
    level.add_order(&order3);

    EXPECT_EQ(level.front(), &order2);
    EXPECT_EQ(level.order_count(), 2);
}

TEST(PriceLevelTest, LargeQuantitySum)
{
    PriceLevel level;

    Order order1(1, 100, 100, Side::Buy, to_price(10.00), 1000000, 1000, OrderType::Limit);
    Order order2(2, 100, 100, Side::Buy, to_price(10.00), 2000000, 2000, OrderType::Limit);
    Order order3(3, 100, 100, Side::Buy, to_price(10.00), 3000000, 3000, OrderType::Limit);

    level.add_order(&order1);
    level.add_order(&order2);
    level.add_order(&order3);

    EXPECT_EQ(level.get_total_quantity(), 6000000);
}
