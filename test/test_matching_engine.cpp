#include "matching_engine.hpp"
#include "price.hpp"

#include <atomic>
#include <gtest/gtest.h>

// ─── SingleSymbolRouting ──────────────────────────────────────────────────────

TEST(MatchingEngineTest, SingleSymbolRouting) {
    MatchingEngine engine;
    engine.route(
        OrderCommand::make_new(1, 1, 1, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));

    ASSERT_NE(engine.get_book(1), nullptr);
    EXPECT_TRUE(engine.get_book(1)->has_order(1));
    EXPECT_EQ(engine.symbol_count(), 1u);
}

// ─── TwoSymbolsIndependent ────────────────────────────────────────────────────

TEST(MatchingEngineTest, TwoSymbolsIndependent) {
    MatchingEngine engine;

    engine.route(
        OrderCommand::make_new(1, 1, 1, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));
    engine.route(
        OrderCommand::make_new(2, 2, 1, Side::Buy, to_price(200.0), 200, 0, OrderType::Limit));

    EXPECT_EQ(engine.symbol_count(), 2u);
    ASSERT_NE(engine.get_book(1), nullptr);
    ASSERT_NE(engine.get_book(2), nullptr);

    // Order 1 is only in book 1
    EXPECT_TRUE(engine.get_book(1)->has_order(1));
    EXPECT_FALSE(engine.get_book(1)->has_order(2));

    // Order 2 is only in book 2
    EXPECT_TRUE(engine.get_book(2)->has_order(2));
    EXPECT_FALSE(engine.get_book(2)->has_order(1));
}

// ─── MatchWithinSymbol ────────────────────────────────────────────────────────

TEST(MatchingEngineTest, MatchWithinSymbol) {
    MatchingEngine engine;
    std::atomic<int> trades{0};
    engine.set_trade_callback(
        [&](const TradeEvent&) { trades.fetch_add(1, std::memory_order_relaxed); });

    // Resting sell on symbol 1
    engine.route(
        OrderCommand::make_new(1, 1, 1, Side::Sell, to_price(100.0), 100, 0, OrderType::Limit));
    // Resting buy on symbol 2 — no match possible across symbols
    engine.route(
        OrderCommand::make_new(2, 2, 2, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));

    EXPECT_EQ(trades.load(), 0);

    // Aggressive buy on symbol 1 — should match with order 1
    engine.route(
        OrderCommand::make_new(3, 1, 2, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));

    EXPECT_EQ(trades.load(), 1);
    // Symbol 2 is unaffected
    EXPECT_TRUE(engine.get_book(2)->has_order(2));
}

// ─── CancelCrossSymbol ────────────────────────────────────────────────────────

TEST(MatchingEngineTest, CancelCrossSymbol) {
    MatchingEngine engine;

    engine.route(
        OrderCommand::make_new(1, 1, 1, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));
    EXPECT_TRUE(engine.get_book(1)->has_order(1));

    // Force-create book 2 and try to cancel order 1 directly from it
    OrderBook& book2 = engine.get_or_create_book(2);
    EXPECT_FALSE(book2.cancel_order(1)); // book 2 does not own order 1
    EXPECT_TRUE(engine.get_book(1)->has_order(1)); // order still in book 1

    EXPECT_EQ(engine.symbol_count(), 2u);
}

// ─── LazyBookCreation ─────────────────────────────────────────────────────────

TEST(MatchingEngineTest, LazyBookCreation) {
    MatchingEngine engine;
    EXPECT_EQ(engine.symbol_count(), 0u);

    engine.route(
        OrderCommand::make_new(1, 5, 1, Side::Buy, to_price(50.0), 10, 0, OrderType::Limit));
    EXPECT_EQ(engine.symbol_count(), 1u);

    engine.route(
        OrderCommand::make_new(2, 5, 1, Side::Sell, to_price(60.0), 10, 0, OrderType::Limit));
    EXPECT_EQ(engine.symbol_count(), 1u); // same symbol

    engine.route(
        OrderCommand::make_new(3, 7, 1, Side::Buy, to_price(50.0), 10, 0, OrderType::Limit));
    EXPECT_EQ(engine.symbol_count(), 2u);
}

// ─── AmendCrossSymbol ─────────────────────────────────────────────────────────

TEST(MatchingEngineTest, AmendCrossSymbol) {
    MatchingEngine engine;

    engine.route(
        OrderCommand::make_new(1, 1, 1, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));
    ASSERT_TRUE(engine.get_book(1)->has_order(1));

    // Attempt amend directly on book 2 (which doesn't have the order)
    OrderBook& book2 = engine.get_or_create_book(2);
    bool result = book2.amend_order(1, 50, to_price(101.0));
    EXPECT_FALSE(result); // no-op: order not in book 2

    // Order in book 1 is unchanged
    ASSERT_TRUE(engine.get_book(1)->has_order(1));
    EXPECT_EQ(engine.get_book(1)->get_order(1)->quantity, 100u);
}

// ─── MarketDataPerSymbol ──────────────────────────────────────────────────────

TEST(MatchingEngineTest, MarketDataPerSymbol) {
    MatchingEngine engine;

    engine.route(
        OrderCommand::make_new(1, 1, 1, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));
    engine.route(
        OrderCommand::make_new(2, 2, 1, Side::Buy, to_price(200.0), 200, 0, OrderType::Limit));

    auto bbo1 = engine.get_book(1)->get_bbo();
    auto bbo2 = engine.get_book(2)->get_bbo();

    EXPECT_EQ(bbo1.bid.price, to_price(100.0));
    EXPECT_EQ(bbo1.bid.quantity, 100u);

    EXPECT_EQ(bbo2.bid.price, to_price(200.0));
    EXPECT_EQ(bbo2.bid.quantity, 200u);
}

// ─── UnknownSymbolReturnsNullptr ──────────────────────────────────────────────

TEST(MatchingEngineTest, UnknownSymbolReturnsNullptr) {
    MatchingEngine engine;
    EXPECT_EQ(engine.get_book(99), nullptr);

    engine.route(
        OrderCommand::make_new(1, 1, 1, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));
    EXPECT_EQ(engine.get_book(2), nullptr); // symbol 2 never seen
    EXPECT_NE(engine.get_book(1), nullptr);
}

// ─── CancelRoutedViaReverseLookup ─────────────────────────────────────────────

TEST(MatchingEngineTest, CancelRoutedViaReverseLookup) {
    MatchingEngine engine;
    std::atomic<int> cancels{0};
    engine.set_order_callback([&](const OrderEvent& e) {
        if (e.type == OrderEventType::Cancelled)
            cancels.fetch_add(1, std::memory_order_relaxed);
    });

    engine.route(
        OrderCommand::make_new(1, 1, 1, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));
    EXPECT_TRUE(engine.get_book(1)->has_order(1));

    // make_cancel does not set symbol_id; engine uses reverse lookup
    engine.route(OrderCommand::make_cancel(1));
    EXPECT_FALSE(engine.get_book(1)->has_order(1));
    EXPECT_EQ(cancels.load(), 1);
}

// ─── AmendRoutedViaReverseLookup ─────────────────────────────────────────────

TEST(MatchingEngineTest, AmendRoutedViaReverseLookup) {
    MatchingEngine engine;
    std::atomic<int> amends{0};
    engine.set_order_callback([&](const OrderEvent& e) {
        if (e.type == OrderEventType::Amended)
            amends.fetch_add(1, std::memory_order_relaxed);
    });

    engine.route(
        OrderCommand::make_new(1, 1, 1, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));

    engine.route(OrderCommand::make_amend(1, to_price(99.0), 50));
    EXPECT_EQ(amends.load(), 1);
    ASSERT_TRUE(engine.get_book(1)->has_order(1));
    EXPECT_EQ(engine.get_book(1)->get_order(1)->quantity, 50u);
}

// ─── CallbacksPropagatedToNewBooks ────────────────────────────────────────────

TEST(MatchingEngineTest, CallbacksPropagatedToNewBooks) {
    MatchingEngine engine;
    std::atomic<int> trades{0};
    // Set callbacks BEFORE any books exist
    engine.set_trade_callback(
        [&](const TradeEvent&) { trades.fetch_add(1, std::memory_order_relaxed); });

    // Creating book + matching should fire the callback set above
    engine.route(
        OrderCommand::make_new(1, 42, 1, Side::Sell, to_price(100.0), 100, 0, OrderType::Limit));
    engine.route(
        OrderCommand::make_new(2, 42, 2, Side::Buy, to_price(100.0), 100, 0, OrderType::Limit));

    EXPECT_EQ(trades.load(), 1);
}

// ─── HundredKOrdersTwoSymbols ─────────────────────────────────────────────────

TEST(MatchingEngineTest, HundredKOrdersTwoSymbols) {
    constexpr int N = 100'000; // 50K per symbol
    MatchingEngine engine;

    std::atomic<int> total_trades{0};
    engine.set_trade_callback(
        [&](const TradeEvent&) { total_trades.fetch_add(1, std::memory_order_relaxed); });

    // Symbol 1: alternating buy/sell at crossing prices -> should match
    for (int i = 1; i <= N; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        Price price = (side == Side::Buy) ? to_price(100.00) : to_price(99.50);
        TraderId trader = (side == Side::Buy) ? 1u : 2u;
        engine.route(OrderCommand::make_new(
            static_cast<OrderId>(i), 1, trader, side, price, 100, 0, OrderType::Limit));
    }

    // Symbol 2: alternating buy/sell at crossing prices -> should match
    for (int i = 1; i <= N; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        Price price = (side == Side::Buy) ? to_price(200.00) : to_price(199.50);
        TraderId trader = (side == Side::Buy) ? 3u : 4u;
        engine.route(OrderCommand::make_new(
            static_cast<OrderId>(N + i), 2, trader, side, price, 100, 0, OrderType::Limit));
    }

    EXPECT_GT(total_trades.load(), 0);
    EXPECT_EQ(engine.symbol_count(), 2u);

    // Verify books are isolated: prices in book 1 ~100, book 2 ~200
    if (auto* b1 = engine.get_book(1)) {
        auto bbo1 = b1->get_bbo();
        if (bbo1.bid.valid) {
            EXPECT_LT(bbo1.bid.price, to_price(150.0));
        }
    }
    if (auto* b2 = engine.get_book(2)) {
        auto bbo2 = b2->get_bbo();
        if (bbo2.bid.valid) {
            EXPECT_GT(bbo2.bid.price, to_price(150.0));
        }
    }
}
