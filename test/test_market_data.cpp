#include "order_book.hpp"

#include <gtest/gtest.h>

class MarketDataTest : public ::testing::Test {
protected:
    OrderBook book;
    std::vector<std::unique_ptr<Order>> orders;

    Order* make_order(Side side, Price price, Quantity qty, TraderId trader = 1) {
        auto o = std::make_unique<Order>(
            generate_order_id(), 1, trader, side, price, qty, get_timestamp_ns(), OrderType::Limit);
        Order* ptr = o.get();
        orders.push_back(std::move(o));
        return ptr;
    }
};

// --- get_bbo ---

TEST_F(MarketDataTest, BBOEmptyBook) {
    auto bbo = book.get_bbo();
    EXPECT_FALSE(bbo.bid.valid);
    EXPECT_FALSE(bbo.ask.valid);
}

TEST_F(MarketDataTest, BBOOnlyBids) {
    book.add_order(make_order(Side::Buy, to_price(10.00), 100));
    auto bbo = book.get_bbo();
    EXPECT_TRUE(bbo.bid.valid);
    EXPECT_EQ(bbo.bid.price, to_price(10.00));
    EXPECT_EQ(bbo.bid.quantity, 100u);
    EXPECT_FALSE(bbo.ask.valid);
}

TEST_F(MarketDataTest, BBOOnlyAsks) {
    book.add_order(make_order(Side::Sell, to_price(10.50), 200));
    auto bbo = book.get_bbo();
    EXPECT_FALSE(bbo.bid.valid);
    EXPECT_TRUE(bbo.ask.valid);
    EXPECT_EQ(bbo.ask.price, to_price(10.50));
    EXPECT_EQ(bbo.ask.quantity, 200u);
}

TEST_F(MarketDataTest, BBOBothSides) {
    book.add_order(make_order(Side::Buy, to_price(10.00), 100));
    book.add_order(make_order(Side::Buy, to_price(9.50), 50));
    book.add_order(make_order(Side::Sell, to_price(10.50), 200));
    book.add_order(make_order(Side::Sell, to_price(11.00), 150));

    auto bbo = book.get_bbo();
    EXPECT_TRUE(bbo.bid.valid);
    EXPECT_TRUE(bbo.ask.valid);
    EXPECT_EQ(bbo.bid.price, to_price(10.00));
    EXPECT_EQ(bbo.ask.price, to_price(10.50));
}

TEST_F(MarketDataTest, BBOAggregatesQuantityAtBestLevel) {
    book.add_order(make_order(Side::Buy, to_price(10.00), 100, 1));
    book.add_order(make_order(Side::Buy, to_price(10.00), 200, 2));

    auto bbo = book.get_bbo();
    EXPECT_EQ(bbo.bid.quantity, 300u);
}

// --- get_depth ---

TEST_F(MarketDataTest, DepthEmptyBook) {
    auto depth = book.get_depth(5);
    EXPECT_TRUE(depth.bids.empty());
    EXPECT_TRUE(depth.asks.empty());
}

TEST_F(MarketDataTest, DepthTopNLevels) {
    book.add_order(make_order(Side::Buy, to_price(10.00), 100));
    book.add_order(make_order(Side::Buy, to_price(9.50), 200));
    book.add_order(make_order(Side::Buy, to_price(9.00), 300));

    auto depth = book.get_depth(2);
    ASSERT_EQ(depth.bids.size(), 2u);
    EXPECT_EQ(depth.bids[0].price, to_price(10.00));
    EXPECT_EQ(depth.bids[1].price, to_price(9.50));
}

TEST_F(MarketDataTest, DepthNLargerThanBook) {
    book.add_order(make_order(Side::Buy, to_price(10.00), 100));

    auto depth = book.get_depth(5);
    ASSERT_EQ(depth.bids.size(), 1u);
    EXPECT_EQ(depth.bids[0].price, to_price(10.00));
    EXPECT_EQ(depth.bids[0].quantity, 100u);
}

TEST_F(MarketDataTest, DepthBidsSortedDescending) {
    book.add_order(make_order(Side::Buy, to_price(9.00), 100));
    book.add_order(make_order(Side::Buy, to_price(10.00), 200));
    book.add_order(make_order(Side::Buy, to_price(9.50), 150));

    auto depth = book.get_depth(3);
    ASSERT_EQ(depth.bids.size(), 3u);
    EXPECT_GT(depth.bids[0].price, depth.bids[1].price);
    EXPECT_GT(depth.bids[1].price, depth.bids[2].price);
}

TEST_F(MarketDataTest, DepthAsksSortedAscending) {
    book.add_order(make_order(Side::Sell, to_price(11.00), 100));
    book.add_order(make_order(Side::Sell, to_price(10.50), 200));
    book.add_order(make_order(Side::Sell, to_price(12.00), 150));

    auto depth = book.get_depth(3);
    ASSERT_EQ(depth.asks.size(), 3u);
    EXPECT_LT(depth.asks[0].price, depth.asks[1].price);
    EXPECT_LT(depth.asks[1].price, depth.asks[2].price);
}

TEST_F(MarketDataTest, DepthAggregatesMultipleOrdersAtSameLevel) {
    book.add_order(make_order(Side::Sell, to_price(10.50), 100, 1));
    book.add_order(make_order(Side::Sell, to_price(10.50), 300, 2));

    auto depth = book.get_depth(5);
    ASSERT_EQ(depth.asks.size(), 1u);
    EXPECT_EQ(depth.asks[0].quantity, 400u);
}

TEST_F(MarketDataTest, DepthZeroReturnsEmpty) {
    book.add_order(make_order(Side::Buy, to_price(10.00), 100));
    auto depth = book.get_depth(0);
    EXPECT_TRUE(depth.bids.empty());
    EXPECT_TRUE(depth.asks.empty());
}

// --- get_snapshot ---

TEST_F(MarketDataTest, SnapshotEmptyBook) {
    auto snap = book.get_snapshot();
    EXPECT_TRUE(snap.bids.empty());
    EXPECT_TRUE(snap.asks.empty());
}

TEST_F(MarketDataTest, SnapshotReturnsFullBook) {
    book.add_order(make_order(Side::Buy, to_price(10.00), 100));
    book.add_order(make_order(Side::Buy, to_price(9.50), 200));
    book.add_order(make_order(Side::Sell, to_price(10.50), 300));
    book.add_order(make_order(Side::Sell, to_price(11.00), 400));

    auto snap = book.get_snapshot();
    EXPECT_EQ(snap.bids.size(), 2u);
    EXPECT_EQ(snap.asks.size(), 2u);
}

TEST_F(MarketDataTest, SnapshotContainsMoreLevelsThanDepth) {
    for (int i = 1; i <= 10; ++i)
        book.add_order(make_order(Side::Buy, to_price(static_cast<double>(i)), 100));

    auto snap = book.get_snapshot();
    auto depth = book.get_depth(5);
    EXPECT_EQ(snap.bids.size(), 10u);
    EXPECT_EQ(depth.bids.size(), 5u);
}
