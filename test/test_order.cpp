#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "../include/order.hpp"
#include <cstddef>

TEST(OrderTest, BasicConstruction)
{
    OrderId id = generate_order_id();
    SymbolId symbol = 1;
    TraderId trader = 100;
    Side side = Side::Buy;
    Price price = to_price(10.50);
    Quantity qty = 100;
    Timestamp ts = get_timestamp_ns();
    OrderType type = OrderType::Limit;

    Order order(id, symbol, trader, side, price, qty, ts, type);

    EXPECT_EQ(order.order_id, id);
    EXPECT_EQ(order.symbol_id, symbol);
    EXPECT_EQ(order.trader_id, trader);
    EXPECT_EQ(order.side, Side::Buy);
    EXPECT_EQ(order.price, price);
    EXPECT_EQ(order.quantity, qty);
    EXPECT_EQ(order.timestamp, ts);
    EXPECT_EQ(order.order_type, OrderType::Limit);
}

TEST(OderTest, DefaultConstruction)
{
    Order order;

    EXPECT_EQ(order.order_id, 0);
    EXPECT_EQ(order.symbol_id, 0);
    EXPECT_EQ(order.side, Side::Buy);
    EXPECT_EQ(order.price, 0);
    EXPECT_EQ(order.quantity, 0);
    EXPECT_EQ(order.timestamp, 0);
    EXPECT_EQ(order.order_type, OrderType::Limit);
}

TEST(OrdertTest, SizeConstraint)
{
    EXPECT_LE(sizeof(Order), 64);
    std::cout << "Order size: " << sizeof(Order) << " bytes" << std::endl;
}

TEST(OrderIdTest, Uniqueness)
{
    OrderId id1 = generate_order_id();
    OrderId id2 = generate_order_id();
    OrderId id3 = generate_order_id();

    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, id3);
}

TEST(OrderIdTest, MonotonicallyIncreasing)
{
    OrderId id1 = generate_order_id();
    OrderId id2 = generate_order_id();
    OrderId id3 = generate_order_id();

    EXPECT_LT(id1, id2);
    EXPECT_LT(id2, id3);
}

TEST(OrderIdTest, GenerateMany)
{
    constexpr int N = 10000;
    std::vector<OrderId> ids;
    ids.reserve(N);

    for (int i = 0; i < N; ++i)
    {
        ids.push_back(generate_order_id());
    }

    for (std::size_t i = 0; i < ids.size() - 1; ++i)
    {
        EXPECT_LT(ids[i], ids[i + 1]);
    }
}

TEST(TimestampTest, MonotonicallyIncreasing)
{
    Timestamp ts1 = get_timestamp_ns();
    Timestamp ts2 = get_timestamp_ns();
    Timestamp ts3 = get_timestamp_ns();

    EXPECT_LE(ts1, ts2);
    EXPECT_LE(ts2, ts3);
}

TEST(TimestampTest, ReasonableValues)
{
    Timestamp ts = get_timestamp_ns();

    EXPECT_GT(ts, 0);
    EXPECT_GT(ts, 1000000ULL);
}

TEST(OrderTest, OutputOperator)
{
    Order order(
        generate_order_id(),
        1,
        100,
        Side::Buy,
        to_price(10.50),
        100,
        get_timestamp_ns(),
        OrderType::Limit
    );

    std::stringstream ss;
    ss << order;
    std::string output = ss.str();

    EXPECT_NE(output.find("Order{"), std::string::npos);
    EXPECT_NE(output.find("Buy"), std::string::npos);
    EXPECT_NE(output.find("10.5000"), std::string::npos);
    EXPECT_NE(output.find("Limit"), std::string::npos);
}

TEST(OrderIdTest, ThreadSafety)
{
    constexpr int NUM_THREADS = 10;
    constexpr int IDS_PER_THREAD = 1000;

    std::vector<std::vector<OrderId>> thread_results(NUM_THREADS);
    std::vector<std::thread> threads;

    for (int i = 0; i < NUM_THREADS; ++i)
    {
        threads.emplace_back([&thread_results, i]()
        {
            thread_results[i].reserve(IDS_PER_THREAD);
            for (int j = 0; j < IDS_PER_THREAD; ++j)
            {
                thread_results[i].push_back(generate_order_id());
            }
        });
    }

    for (auto &thread : threads)
    {
        thread.join();
    }

    std::vector<OrderId> all_ids;
    for (const auto &thread_ids : thread_results)
    {
        all_ids.insert(all_ids.end(), thread_ids.begin(), thread_ids.end());
    }

    std::sort(all_ids.begin(), all_ids.end());
    auto it = std::adjacent_find(all_ids.begin(), all_ids.end());

    EXPECT_EQ(it, all_ids.end()) << "Found duplicate OrderId: " << *it;
    EXPECT_EQ(all_ids.size(), NUM_THREADS * IDS_PER_THREAD);
}

