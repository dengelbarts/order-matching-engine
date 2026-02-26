#include <gtest/gtest.h>
#include "../include/object_pool.hpp"
#include "../include/order.hpp"
#include "../include/order_book.hpp"

TEST(ObjectPoolTest, BasicAllocateDeallocate)
{
    ObjectPool<Order, 8> pool;
    EXPECT_EQ(pool.capacity(), 8u);
    EXPECT_EQ(pool.available(), 8u);

    Order *o = pool.allocate(1, 1, 1, Side::Buy, 10000, 100, 0, OrderType::Limit);
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(o->order_id, 1u);
    EXPECT_EQ(o->price, 10000);
    EXPECT_EQ(pool.available(), 7u);
    EXPECT_EQ(pool.get_stats().in_use, 1u);

    pool.deallocate(o);
    EXPECT_EQ(pool.available(), 8u);
    EXPECT_EQ(pool.get_stats().in_use, 0u);
}

TEST(ObjectPoolTest, SlotInReuseAfterDeallocate)
{
    ObjectPool<Order, 4> pool;

    Order *o1 = pool.allocate(1, 1, 1, Side::Buy, 10000, 100, 0, OrderType::Limit);
    void *addr = static_cast<void*>(o1);
    pool.deallocate(o1);

    Order *o2 = pool.allocate(2, 1, 1, Side::Buy, 20000, 200, 0, OrderType::Limit);
    EXPECT_EQ(static_cast<void*>(o2), addr);
    EXPECT_EQ(o2->order_id, 2u);
    EXPECT_EQ(o2->price, 20000);

    pool.deallocate(o2);
}

TEST(ObjectPoolTest, AllocateDeallocateCycles)
{
    ObjectPool<Order, 8> pool;

    for (int i = 0; i < 200; i++)
    {
        Order *o = pool.allocate(i + 1, 1, 1, Side::Buy, 10000, 100, 0, OrderType::Limit);
        EXPECT_NE(o, nullptr);
        EXPECT_TRUE(pool.is_from_pool(o));
        pool.deallocate(o);
        EXPECT_EQ(pool.available(), 8u);
    }
}

TEST(ObjectPoolTest, IsFromPool)
{
    ObjectPool<Order, 4> pool;

    Order *pool_order = pool.allocate(1, 1, 1, Side::Buy, 10000, 100, 0, OrderType::Limit);
    EXPECT_TRUE(pool.is_from_pool(pool_order));

    Order stack_order(2, 1, 1, Side::Sell, 10000, 100, 0, OrderType::Limit);
    EXPECT_FALSE(pool.is_from_pool(&stack_order));

    pool.deallocate(pool_order);
}

TEST(ObjectPoolTest, HighWaterMark)
{
    ObjectPool<Order, 8> pool;

    Order *orders[5];
    for (int i = 0; i < 5; i++)
        orders[i] = pool.allocate(i + 1, 1, 1, Side::Buy, 10000, 100, 0, OrderType::Limit);

    EXPECT_EQ(pool.get_stats().high_water_mark, 5u);

    pool.deallocate(orders[0]);
    pool.deallocate(orders[1]);

    EXPECT_EQ(pool.get_stats().in_use, 3u);
    EXPECT_EQ(pool.get_stats().high_water_mark, 5u);

    for (int i = 2; i < 5; i++)
        pool.deallocate(orders[i]);
}

TEST(ObjectPoolTest, PoolExhaustionFallBackToHeap)
{
    ObjectPool<Order, 4> pool;

    Order *orders[6];
    for (int i = 0; i < 4; i++)
        orders[i] = pool.allocate(i + 1, 1, 1, Side::Buy, 10000, 100, 0, OrderType::Limit);

    EXPECT_EQ(pool.available(), 0u);
    EXPECT_EQ(pool.get_stats().heap_fallbacks, 0u);

    orders[4] = pool.allocate(5, 1, 1, Side::Buy, 10000, 100, 0, OrderType::Limit);
    orders[5] = pool.allocate(6, 1, 1, Side::Buy, 10000, 100, 0, OrderType::Limit);

    ASSERT_NE(orders[4], nullptr);
    ASSERT_NE(orders[5], nullptr);
    EXPECT_EQ(pool.get_stats().heap_fallbacks, 2u);
    EXPECT_FALSE(pool.is_from_pool(orders[4]));
    EXPECT_FALSE(pool.is_from_pool(orders[5]));
    
    for (int i = 0; i < 6; i++)
        pool.deallocate(orders[i]);
    
    EXPECT_EQ(pool.available(), 4u);
    EXPECT_EQ(pool.get_stats().in_use, 0u);
}

TEST(ObjectPoolTest, OrderBookCreateOrderRests)
{
    OrderBook book;

    Order *o = book.create_order(1, 1, 1, Side::Buy, 10000, 100, 0, OrderType::Limit);

    EXPECT_NE(o, nullptr);
    EXPECT_TRUE(book.has_order(1));
    EXPECT_EQ(book.get_pool_stats().in_use, 1u);
}

TEST(ObjectPoolTest, OrderBookCancelFreesPool)
{
    OrderBook book;
    book.create_order(1, 1, 1, Side::Buy, 10000, 100, 0, OrderType::Limit);
    EXPECT_EQ(book.get_pool_stats().in_use, 1u);

    book.cancel_order(1);

    EXPECT_FALSE(book.has_order(1));
    EXPECT_EQ(book.get_pool_stats().in_use, 0u);
}

TEST(ObjectPoolTest, OrderBookMatchFreesPool)
{
    OrderBook book;

    book.create_order(1, 1, 1, Side::Buy, 10000, 100, 0, OrderType::Limit);
    EXPECT_EQ(book.get_pool_stats().in_use, 1u);

    Order *sell = book.create_order(2, 1, 2, Side::Sell, 10000, 100, 0, OrderType::Limit);
    EXPECT_EQ(sell, nullptr);
    EXPECT_FALSE(book.has_order(1));
    EXPECT_FALSE(book.has_order(2));
    EXPECT_EQ(book.get_pool_stats().in_use, 0u);
}

TEST(ObjectPoolTest, OrderBookHighWaterMark)
{
    OrderBook book;

    for (int i = 1; i <= 5; i++)
        book.create_order(i, 1, 1, Side::Buy, 10000 + i, 100, 0, OrderType::Limit);

    EXPECT_EQ(book.get_pool_stats().in_use, 5u);
    EXPECT_EQ(book.get_pool_stats().high_water_mark, 5u);

    for (int i = 1; i <= 5; i++)
        book.cancel_order(i);

    EXPECT_EQ(book.get_pool_stats().in_use, 0u);
    EXPECT_EQ(book.get_pool_stats().high_water_mark, 5u);
}