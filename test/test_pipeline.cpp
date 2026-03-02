#include <gtest/gtest.h>
#include <atomic>
#include "../include/matching_pipeline.hpp"
#include "../include/price.hpp"

TEST(MatchingPipelineTest, StartAndShutdown)
{
    MatchingPipeline pipeline;
    pipeline.start();
    pipeline.shutdown();
    EXPECT_EQ(pipeline.processed(), 0u);
}

TEST(MatchingPipelineTest, SingleNewOrder)
{
    MatchingPipeline pipeline;
    pipeline.start();

    pipeline.submit(OrderCommand::make_new(1, 1, 1, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));
    pipeline.shutdown();
    EXPECT_EQ(pipeline.processed(), 1u);
}

TEST(MatchingPipelineTest, TradeCallbackFires)
{
    MatchingPipeline pipeline;
    std::atomic<int> trade_count{0};

    pipeline.set_trade_callback([&](const TradeEvent&) { trade_count.fetch_add(1, std::memory_order_relaxed);});
    pipeline.start();

    pipeline.submit(OrderCommand::make_new(1, 1, 1, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));
    pipeline.submit(OrderCommand::make_new(2, 1, 2, Side::Sell, to_price(100.0), 100, 0, OrderType::Limit));

    pipeline.shutdown();

    EXPECT_EQ(trade_count.load(), 1);
    EXPECT_EQ(pipeline.processed(), 2u);
}

TEST(MatchingPipelineTest, CancelCommandProcessed)
{
    MatchingPipeline pipeline;
    std::atomic<int> cancel_count{0};

    pipeline.set_order_callback([&](const OrderEvent &e)
    {
        if (e.type == OrderEventType::Cancelled)
            cancel_count.fetch_add(1, std::memory_order_relaxed);
    });
    pipeline.start();

    pipeline.submit(OrderCommand::make_new(1, 1, 1, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));
    pipeline.submit(OrderCommand::make_cancel(1));
    pipeline.shutdown();

    EXPECT_EQ(cancel_count.load(), 1);
    EXPECT_EQ(pipeline.processed(), 2u);
}

TEST(MatchingPipelineTest, AmendCommandProcessed)
{
    MatchingPipeline pipeline;
    std::atomic<int> amend_count{0};

    pipeline.set_order_callback([&](const OrderEvent &e)
    {
        if (e.type == OrderEventType::Amended)
            amend_count.fetch_add(1, std::memory_order_relaxed);
    });
    pipeline.start();

    pipeline.submit(OrderCommand::make_new(1, 1, 1, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));
    pipeline.submit(OrderCommand::make_amend(1, to_price(101.0), 50));
    pipeline.shutdown();

    EXPECT_EQ(amend_count.load(), 1);
}

TEST(MatchingPipelineTest, HundredKOrdersThroughPipeline)
{
    constexpr int N = 100'000;
    MatchingPipeline pipeline;
    std::atomic<int> trade_count{0};

    pipeline.set_trade_callback([&](const TradeEvent&) { trade_count.fetch_add(1, std::memory_order_relaxed); });
    pipeline.start();

    const Price BUY_PRICE = to_price(100.00);
    const Price SELL_PRICE = to_price(99.50);

    for (int i = 1; i <= N; ++i)
    {
        Side     side   = (i % 2 == 0) ? Side::Buy : Side::Sell;
        Price    price  = (side == Side::Buy) ? BUY_PRICE : SELL_PRICE;
        TraderId trader = (side == Side::Buy) ? 1 : 2;  // different traders — avoid self-match prevention
        pipeline.submit(OrderCommand::make_new(static_cast<OrderId>(i), 1, trader, side, price, 100, 0, OrderType::Limit));
    }
    pipeline.shutdown();

    EXPECT_EQ(pipeline.processed(), static_cast<uint64_t>(N));
    EXPECT_GT(trade_count.load(), 0);
}

TEST(MatchingPipelineTest, CleanShutdownNoLostOrders)
{
    constexpr int N = 100'000;
    MatchingPipeline pipeline;
    std::atomic<int> new_events{0};

    pipeline.set_order_callback([&](const OrderEvent& e) {
        if (e.type == OrderEventType::New)
            new_events.fetch_add(1, std::memory_order_relaxed);
    });
    pipeline.start();

    for (int i = 1; i <= N; ++i)
        pipeline.submit(OrderCommand::make_new(static_cast<OrderId>(i), 1, 1, Side::Buy, to_price(100.0) + i, 100, 0, OrderType::Limit));

    pipeline.shutdown();

    EXPECT_EQ(pipeline.processed(), static_cast<uint64_t>(N));
    EXPECT_EQ(new_events.load(), N);
}