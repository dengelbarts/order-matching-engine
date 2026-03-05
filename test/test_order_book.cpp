#include "../include/order_book.hpp"

#include <gtest/gtest.h>

class OrderBookTest : public ::testing::Test {
protected:
    OrderBook book;
    std::vector<std::unique_ptr<Order>> orders;

    Order* create_order(Side side, Price price, Quantity qty, TraderId trader = 100) {
        auto order = std::make_unique<Order>(
            generate_order_id(), 1, trader, side, price, qty, get_timestamp_ns(), OrderType::Limit);
        Order* ptr = order.get();
        orders.push_back(std::move(order));
        return ptr;
    }
};

TEST_F(OrderBookTest, AddSingleBuyOrder) {
    Order* order = create_order(Side::Buy, to_price(10.50), 100);
    book.add_order(order);

    EXPECT_TRUE(book.has_order(order->order_id));
    EXPECT_EQ(book.get_order(order->order_id), order);

    const auto& bids = book.get_bids();
    EXPECT_EQ(bids.size(), 1);
    EXPECT_TRUE(bids.find(to_price(10.50)) != bids.end());
}

TEST_F(OrderBookTest, AddSingleSellOrder) {
    Order* order = create_order(Side::Sell, to_price(10.75), 50);
    book.add_order(order);

    EXPECT_TRUE(book.has_order(order->order_id));

    const auto& asks = book.get_asks();
    EXPECT_EQ(asks.size(), 1);
    EXPECT_TRUE(asks.find(to_price(10.75)) != asks.end());
}

TEST_F(OrderBookTest, AddMultipleOrdersAtSamePrice) {
    Order* order1 = create_order(Side::Buy, to_price(10.00), 100);
    Order* order2 = create_order(Side::Buy, to_price(10.00), 200);
    Order* order3 = create_order(Side::Buy, to_price(10.00), 150);

    book.add_order(order1);
    book.add_order(order2);
    book.add_order(order3);

    const auto& bids = book.get_bids();
    EXPECT_EQ(bids.size(), 1);

    auto it = bids.find(to_price(10.00));
    ASSERT_TRUE(it != bids.end());

    const PriceLevel& level = it->second;
    EXPECT_EQ(level.order_count(), 3);
    EXPECT_EQ(level.get_total_quantity(), 450);
}

TEST_F(OrderBookTest, AddOrdersOnBothSides) {
    Order* buy1 = create_order(Side::Buy, to_price(10.00), 100);
    Order* buy2 = create_order(Side::Buy, to_price(9.50), 200);
    Order* sell1 = create_order(Side::Sell, to_price(10.50), 150);
    Order* sell2 = create_order(Side::Sell, to_price(11.00), 75);

    book.add_order(buy1);
    book.add_order(buy2);
    book.add_order(sell1);
    book.add_order(sell2);

    const auto& bids = book.get_bids();
    const auto& asks = book.get_asks();

    EXPECT_EQ(bids.size(), 2);
    EXPECT_EQ(asks.size(), 2);

    EXPECT_TRUE(book.has_order(buy1->order_id));
    EXPECT_TRUE(book.has_order(buy2->order_id));
    EXPECT_TRUE(book.has_order(sell1->order_id));
    EXPECT_TRUE(book.has_order(sell2->order_id));
}

TEST_F(OrderBookTest, BidsSortedDescending) {
    Order* order1 = create_order(Side::Buy, to_price(10.00), 100);
    Order* order2 = create_order(Side::Buy, to_price(10.50), 100);
    Order* order3 = create_order(Side::Buy, to_price(9.50), 100);

    book.add_order(order1);
    book.add_order(order2);
    book.add_order(order3);

    const auto& bids = book.get_bids();
    auto it = bids.begin();

    EXPECT_EQ(it->first, to_price(10.50));
    ++it;
    EXPECT_EQ(it->first, to_price(10.00));
    ++it;
    EXPECT_EQ(it->first, to_price(9.50));
}

TEST_F(OrderBookTest, AsksSortedAscending) {
    Order* order1 = create_order(Side::Sell, to_price(11.00), 100);
    Order* order2 = create_order(Side::Sell, to_price(10.50), 100);
    Order* order3 = create_order(Side::Sell, to_price(11.50), 100);

    book.add_order(order1);
    book.add_order(order2);
    book.add_order(order3);

    const auto& asks = book.get_asks();
    auto it = asks.begin();

    EXPECT_EQ(it->first, to_price(10.50));
    ++it;
    EXPECT_EQ(it->first, to_price(11.00));
    ++it;
    EXPECT_EQ(it->first, to_price(11.50));
}

TEST_F(OrderBookTest, OrderBookRetrievalById) {
    Order* order = create_order(Side::Buy, to_price(10.00), 100);
    book.add_order(order);

    Order* retrieved = book.get_order(order->order_id);
    EXPECT_EQ(retrieved, order);
    EXPECT_EQ(retrieved->order_id, order->order_id);
    EXPECT_EQ(retrieved->price, to_price(10.00));
    EXPECT_EQ(retrieved->quantity, 100);
}

TEST_F(OrderBookTest, NonExistentOrderLookup) {
    EXPECT_FALSE(book.has_order(999999));
    EXPECT_EQ(book.get_order(999999), nullptr);
}

TEST_F(OrderBookTest, CancelExistingOrder) {
    Order* order = create_order(Side::Buy, to_price(10.00), 100);
    book.add_order(order);

    EXPECT_TRUE(book.has_order(order->order_id));

    bool cancelled = book.cancel_order(order->order_id);

    EXPECT_TRUE(cancelled);
    EXPECT_FALSE(book.has_order(order->order_id));
    EXPECT_EQ(book.get_order(order->order_id), nullptr);

    const auto& bids = book.get_bids();
    EXPECT_EQ(bids.size(), 0);
}

TEST_F(OrderBookTest, CancelNonExistentOrder) {
    bool cancelled = book.cancel_order(999999);
    EXPECT_FALSE(cancelled);
}

TEST_F(OrderBookTest, CancelRemovesOnlyTargetOrder) {
    Order* order1 = create_order(Side::Buy, to_price(10.00), 100);
    Order* order2 = create_order(Side::Buy, to_price(10.00), 200);
    Order* order3 = create_order(Side::Buy, to_price(10.00), 150);

    book.add_order(order1);
    book.add_order(order2);
    book.add_order(order3);

    bool cancelled = book.cancel_order(order2->order_id);

    EXPECT_TRUE(cancelled);
    EXPECT_TRUE(book.has_order(order1->order_id));
    EXPECT_FALSE(book.has_order(order2->order_id));
    EXPECT_TRUE(book.has_order(order3->order_id));

    const auto& bids = book.get_bids();
    EXPECT_EQ(bids.size(), 1);

    auto it = bids.find(to_price(10.00));
    ASSERT_TRUE(it != bids.end());
    EXPECT_EQ(it->second.order_count(), 2);
    EXPECT_EQ(it->second.get_total_quantity(), 250);
}

TEST_F(OrderBookTest, CancelLastOrderAtPriceRemovesPriceLevel) {
    Order* order1 = create_order(Side::Buy, to_price(10.00), 100);
    Order* order2 = create_order(Side::Buy, to_price(9.50), 200);

    book.add_order(order1);
    book.add_order(order2);

    const auto& bids = book.get_bids();
    EXPECT_EQ(bids.size(), 2);

    book.cancel_order(order1->order_id);

    EXPECT_EQ(bids.size(), 1);
    EXPECT_TRUE(bids.find(to_price(10.00)) == bids.end());
    EXPECT_TRUE(bids.find(to_price(9.50)) != bids.end());
}

TEST_F(OrderBookTest, GetBestBidOnEmptyBook) {
    auto bbo = book.get_best_bid();

    EXPECT_FALSE(bbo.valid);
    EXPECT_EQ(bbo.price, 0);
    EXPECT_EQ(bbo.quantity, 0);
}

TEST_F(OrderBookTest, GetBestAskOnEmptyBook) {
    auto bbo = book.get_best_ask();

    EXPECT_FALSE(bbo.valid);
    EXPECT_EQ(bbo.price, 0);
    EXPECT_EQ(bbo.quantity, 0);
}

TEST_F(OrderBookTest, GetBestBidWithSingleOrder) {
    Order* order = create_order(Side::Buy, to_price(10.00), 100);
    book.add_order(order);

    auto bbo = book.get_best_bid();

    EXPECT_TRUE(bbo.valid);
    EXPECT_EQ(bbo.price, to_price(10.00));
    EXPECT_EQ(bbo.quantity, 100);
}

TEST_F(OrderBookTest, GetBestAskWithSingleOrder) {
    Order* order = create_order(Side::Sell, to_price(10.50), 75);
    book.add_order(order);

    auto bbo = book.get_best_ask();

    EXPECT_TRUE(bbo.valid);
    EXPECT_EQ(bbo.price, to_price(10.50));
    EXPECT_EQ(bbo.quantity, 75);
}

TEST_F(OrderBookTest, GetBestBidWithMultiplePriceLevels) {
    Order* order1 = create_order(Side::Buy, to_price(10.00), 100);
    Order* order2 = create_order(Side::Buy, to_price(10.50), 200);
    Order* order3 = create_order(Side::Buy, to_price(9.75), 150);

    book.add_order(order1);
    book.add_order(order2);
    book.add_order(order3);

    auto bbo = book.get_best_bid();

    EXPECT_TRUE(bbo.valid);
    EXPECT_EQ(bbo.price, to_price(10.50));
    EXPECT_EQ(bbo.quantity, 200);
}

TEST_F(OrderBookTest, GetBestAskWithMultiplePriceLevels) {
    Order* order1 = create_order(Side::Sell, to_price(11.00), 100);
    Order* order2 = create_order(Side::Sell, to_price(10.50), 200);
    Order* order3 = create_order(Side::Sell, to_price(11.25), 150);

    book.add_order(order1);
    book.add_order(order2);
    book.add_order(order3);

    auto bbo = book.get_best_ask();

    EXPECT_TRUE(bbo.valid);
    EXPECT_EQ(bbo.price, to_price(10.50));
    EXPECT_EQ(bbo.quantity, 200);
}

TEST_F(OrderBookTest, GetBestBidAggregatesQuantityAtSamePrice) {
    Order* order1 = create_order(Side::Buy, to_price(10.00), 100);
    Order* order2 = create_order(Side::Buy, to_price(10.00), 200);
    Order* order3 = create_order(Side::Buy, to_price(10.00), 50);

    book.add_order(order1);
    book.add_order(order2);
    book.add_order(order3);

    auto bbo = book.get_best_bid();

    EXPECT_TRUE(bbo.valid);
    EXPECT_EQ(bbo.price, to_price(10.00));
    EXPECT_EQ(bbo.quantity, 350);
}

TEST_F(OrderBookTest, CancelBestBidUpdatesBBO) {
    Order* order1 = create_order(Side::Buy, to_price(10.50), 100);
    Order* order2 = create_order(Side::Buy, to_price(10.00), 200);

    book.add_order(order1);
    book.add_order(order2);

    auto bbo = book.get_best_bid();
    EXPECT_EQ(bbo.price, to_price(10.50));

    book.cancel_order(order1->order_id);

    bbo = book.get_best_bid();
    EXPECT_TRUE(bbo.valid);
    EXPECT_EQ(bbo.price, to_price(10.00));
    EXPECT_EQ(bbo.quantity, 200);
}

TEST_F(OrderBookTest, CancelBestAskUpdatesBBO) {
    Order* order1 = create_order(Side::Sell, to_price(10.50), 100);
    Order* order2 = create_order(Side::Sell, to_price(11.00), 200);

    book.add_order(order1);
    book.add_order(order2);

    auto bbo = book.get_best_ask();
    EXPECT_EQ(bbo.price, to_price(10.50));

    book.cancel_order(order1->order_id);

    bbo = book.get_best_ask();
    EXPECT_TRUE(bbo.valid);
    EXPECT_EQ(bbo.price, to_price(11.00));
    EXPECT_EQ(bbo.quantity, 200);
}

TEST_F(OrderBookTest, GetSpreadOnEmptyBook) {
    auto spread = book.get_spread();

    EXPECT_FALSE(spread.valid);
    EXPECT_EQ(spread.value, 0);
}

TEST_F(OrderBookTest, GetSpreadWithOnlyBids) {
    Order* order = create_order(Side::Buy, to_price(10.00), 100);
    book.add_order(order);

    auto spread = book.get_spread();

    EXPECT_FALSE(spread.valid);
}

TEST_F(OrderBookTest, GetSpreadWithOnlyAsks) {
    Order* order = create_order(Side::Sell, to_price(10.50), 100);
    book.add_order(order);

    auto spread = book.get_spread();

    EXPECT_FALSE(spread.valid);
}

TEST_F(OrderBookTest, GetSpreadWithBothSides) {
    Order* bid = create_order(Side::Buy, to_price(10.00), 100);
    Order* ask = create_order(Side::Sell, to_price(10.50), 100);

    book.add_order(bid);
    book.add_order(ask);

    auto spread = book.get_spread();

    EXPECT_TRUE(spread.valid);
    EXPECT_EQ(spread.value, to_price(0.50));
}

TEST_F(OrderBookTest, GetSpreadWithTightMarket) {
    Order* bid = create_order(Side::Buy, to_price(10.00), 100);
    Order* ask = create_order(Side::Sell, to_price(10.01), 100);

    book.add_order(bid);
    book.add_order(ask);

    auto spread = book.get_spread();

    EXPECT_TRUE(spread.valid);
    EXPECT_EQ(spread.value, to_price(0.01));
}

TEST_F(OrderBookTest, GetSpreadWithWideMarket) {
    Order* bid = create_order(Side::Buy, to_price(10.00), 100);
    Order* ask = create_order(Side::Sell, to_price(15.00), 100);

    book.add_order(bid);
    book.add_order(ask);

    auto spread = book.get_spread();

    EXPECT_TRUE(spread.valid);
    EXPECT_EQ(spread.value, to_price(5.00));
}

TEST_F(OrderBookTest, SpreadUpdatesAfterCancel) {
    Order* bid1 = create_order(Side::Buy, to_price(10.00), 100);
    Order* bid2 = create_order(Side::Buy, to_price(9.50), 100);
    Order* ask = create_order(Side::Sell, to_price(10.50), 100);

    book.add_order(bid1);
    book.add_order(bid2);
    book.add_order(ask);

    auto spread = book.get_spread();
    EXPECT_EQ(spread.value, to_price(0.50));

    book.cancel_order(bid1->order_id);

    spread = book.get_spread();
    EXPECT_TRUE(spread.valid);
    EXPECT_EQ(spread.value, to_price(1.00));
}
