#include "../include/order_book.hpp"
#include "../include/trade.hpp"

#include <memory>
#include <vector>

#include <gtest/gtest.h>

class MatchingTest : public ::testing::Test {
protected:
    OrderBook book;
    std::vector<std::unique_ptr<Order>> orders;
    SymbolId symbol = 1;

    Order* create_order(Side side,
                        Price price,
                        Quantity qty,
                        TraderId trader = 100,
                        OrderType type = OrderType::Limit) {
        auto order = std::make_unique<Order>(
            generate_order_id(), symbol, trader, side, price, qty, get_timestamp_ns(), type);
        Order* ptr = order.get();
        orders.push_back(std::move(order));
        return ptr;
    }
};

TEST_F(MatchingTest, ExactMatch) {
    Order* sell = create_order(Side::Sell, to_price(10.00), 100, 200);
    book.add_order(sell);

    Order* buy = create_order(Side::Buy, to_price(10.00), 100, 100);
    std::vector<Trade> trades = book.match(buy);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].buy_order_id, buy->order_id);
    EXPECT_EQ(trades[0].sell_order_id, sell->order_id);
    EXPECT_EQ(trades[0].price, to_price(10.00));
    EXPECT_EQ(trades[0].quantity, 100);

    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.has_order(sell->order_id));

    auto best_bid = book.get_best_bid();
    auto best_ask = book.get_best_ask();
    EXPECT_FALSE(best_bid.valid);
    EXPECT_FALSE(best_ask.valid);
}

TEST_F(MatchingTest, PartialFillBuyRemains) {
    Order* sell = create_order(Side::Sell, to_price(10.00), 50, 200);
    book.add_order(sell);

    Order* buy = create_order(Side::Buy, to_price(10.00), 100, 100);
    std::vector<Trade> trades = book.match(buy);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 50);
    EXPECT_EQ(trades[0].price, to_price(10.00));

    EXPECT_FALSE(book.has_order(sell->order_id));

    EXPECT_TRUE(book.has_order(buy->order_id));
    Order* remaining_buy = book.get_order(buy->order_id);
    EXPECT_EQ(remaining_buy->quantity, 50);

    auto best_bid = book.get_best_bid();
    EXPECT_TRUE(best_bid.valid);
    EXPECT_EQ(best_bid.price, to_price(10.00));
    EXPECT_EQ(best_bid.quantity, 50);
}

TEST_F(MatchingTest, PriceImprovement) {
    Order* sell = create_order(Side::Sell, to_price(10.00), 100, 200);
    book.add_order(sell);

    Order* buy = create_order(Side::Buy, to_price(10.50), 100, 100);
    std::vector<Trade> trades = book.match(buy);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].price, to_price(10.00));
    EXPECT_EQ(trades[0].quantity, 100);

    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.has_order(sell->order_id));
}

TEST_F(MatchingTest, NoMatch) {
    Order* sell = create_order(Side::Sell, to_price(10.00), 100, 200);
    book.add_order(sell);

    Order* buy = create_order(Side::Buy, to_price(9.00), 100, 100);
    std::vector<Trade> trades = book.match(buy);

    EXPECT_EQ(trades.size(), 0);

    EXPECT_TRUE(book.has_order(buy->order_id));
    EXPECT_TRUE(book.has_order(sell->order_id));

    auto best_bid = book.get_best_bid();
    auto best_ask = book.get_best_ask();
    EXPECT_TRUE(best_bid.valid);
    EXPECT_TRUE(best_ask.valid);
    EXPECT_EQ(best_bid.price, to_price(9.00));
    EXPECT_EQ(best_ask.price, to_price(10.00));
}

TEST_F(MatchingTest, SellMatchesAgainstBid) {
    Order* buy = create_order(Side::Buy, to_price(10.00), 100, 100);
    book.add_order(buy);

    Order* sell = create_order(Side::Sell, to_price(10.00), 100, 200);
    std::vector<Trade> trades = book.match(sell);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].price, to_price(10.00));
    EXPECT_EQ(trades[0].quantity, 100);

    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_FALSE(book.has_order(sell->order_id));
}

TEST_F(MatchingTest, PartialFillSellRemains) {
    Order* buy = create_order(Side::Buy, to_price(10.00), 50, 100);
    book.add_order(buy);

    Order* sell = create_order(Side::Sell, to_price(10.00), 100, 200);
    std::vector<Trade> trades = book.match(sell);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 50);

    EXPECT_FALSE(book.has_order(buy->order_id));
    EXPECT_TRUE(book.has_order(sell->order_id));

    Order* remaining_sell = book.get_order(sell->order_id);
    EXPECT_EQ(remaining_sell->quantity, 50);
}

TEST_F(MatchingTest, FIFOOrdering) {
    Order* sell1 = create_order(Side::Sell, to_price(10.00), 50, 200);
    Order* sell2 = create_order(Side::Sell, to_price(10.00), 50, 300);
    book.add_order(sell1);
    book.add_order(sell2);

    Order* buy = create_order(Side::Buy, to_price(10.00), 50, 100);
    std::vector<Trade> trades = book.match(buy);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].sell_order_id, sell1->order_id);

    EXPECT_FALSE(book.has_order(sell1->order_id));
    EXPECT_TRUE(book.has_order(sell2->order_id));
}

TEST_F(MatchingTest, MultiLevelMatching) {
    Order* sell1 = create_order(Side::Sell, to_price(9.50), 50, 200);
    Order* sell2 = create_order(Side::Sell, to_price(10.00), 50, 300);
    Order* sell3 = create_order(Side::Sell, to_price(10.50), 50, 400);
    book.add_order(sell1);
    book.add_order(sell2);
    book.add_order(sell3);

    Order* buy = create_order(Side::Buy, to_price(10.00), 100, 100);
    std::vector<Trade> trades = book.match(buy);

    ASSERT_EQ(trades.size(), 2);

    EXPECT_EQ(trades[0].price, to_price(9.50));
    EXPECT_EQ(trades[0].quantity, 50);
    EXPECT_EQ(trades[0].sell_order_id, sell1->order_id);

    EXPECT_EQ(trades[1].price, to_price(10.00));
    EXPECT_EQ(trades[1].quantity, 50);
    EXPECT_EQ(trades[1].sell_order_id, sell2->order_id);

    EXPECT_FALSE(book.has_order(sell1->order_id));
    EXPECT_FALSE(book.has_order(sell2->order_id));

    EXPECT_TRUE(book.has_order(sell3->order_id));

    EXPECT_FALSE(book.has_order(buy->order_id));

    auto best_ask = book.get_best_ask();
    EXPECT_TRUE(best_ask.valid);
    EXPECT_EQ(best_ask.price, to_price(10.50));
    EXPECT_EQ(best_ask.quantity, 50);
}

TEST_F(MatchingTest, MultiLevelMatchingSell) {
    Order* buy1 = create_order(Side::Buy, to_price(10.50), 30, 100);
    Order* buy2 = create_order(Side::Buy, to_price(10.00), 40, 200);
    Order* buy3 = create_order(Side::Buy, to_price(9.50), 30, 300);
    book.add_order(buy1);
    book.add_order(buy2);
    book.add_order(buy3);

    Order* sell = create_order(Side::Sell, to_price(9.50), 100, 400);
    std::vector<Trade> trades = book.match(sell);

    ASSERT_EQ(trades.size(), 3);

    EXPECT_EQ(trades[0].price, to_price(10.50));
    EXPECT_EQ(trades[0].quantity, 30);

    EXPECT_EQ(trades[1].price, to_price(10.00));
    EXPECT_EQ(trades[1].quantity, 40);

    EXPECT_EQ(trades[2].price, to_price(9.50));
    EXPECT_EQ(trades[2].quantity, 30);

    EXPECT_FALSE(book.has_order(buy1->order_id));
    EXPECT_FALSE(book.has_order(buy2->order_id));
    EXPECT_FALSE(book.has_order(buy3->order_id));
    EXPECT_FALSE(book.has_order(sell->order_id));

    EXPECT_FALSE(book.get_best_bid().valid);
    EXPECT_FALSE(book.get_best_ask().valid);
}

TEST_F(MatchingTest, SelfMatchPreventionBuy) {
    Order* sell1 = create_order(Side::Sell, to_price(10.00), 50, 100);
    Order* sell2 = create_order(Side::Sell, to_price(10.00), 50, 200);
    book.add_order(sell1);
    book.add_order(sell2);

    // Buy from the same trader as the resting sells.  Self-match prevention
    // skips the entire ask level.  Resting the buy would create a crossed book
    // (bid@10.00 >= ask@10.00), so the engine cancels the incoming order.
    Order* buy = create_order(Side::Buy, to_price(10.00), 100, 100);
    auto trades = book.match(buy);

    EXPECT_EQ(trades.size(), 0);

    // Buy must NOT be in the book — it was cancelled to prevent a crossing.
    EXPECT_FALSE(book.has_order(buy->order_id));

    // Resting sell orders are untouched.
    EXPECT_TRUE(book.has_order(sell1->order_id));
    EXPECT_TRUE(book.has_order(sell2->order_id));
    EXPECT_EQ(sell1->quantity, 50);
    EXPECT_EQ(sell2->quantity, 50);
}

TEST_F(MatchingTest, SelfMatchPreventionSell) {
    Order* buy1 = create_order(Side::Buy, to_price(10.00), 50, 100);
    Order* buy2 = create_order(Side::Buy, to_price(10.00), 50, 200);
    book.add_order(buy1);
    book.add_order(buy2);

    // Sell from the same trader as the resting buys.  Resting would create a
    // crossed book (bid@10.00 >= ask@10.00), so the engine cancels the order.
    Order* sell = create_order(Side::Sell, to_price(10.00), 100, 100);
    auto trades = book.match(sell);

    EXPECT_EQ(trades.size(), 0);

    // Sell must NOT be in the book.
    EXPECT_FALSE(book.has_order(sell->order_id));

    // Resting buy orders are untouched.
    EXPECT_TRUE(book.has_order(buy1->order_id));
    EXPECT_TRUE(book.has_order(buy2->order_id));
    EXPECT_EQ(buy1->quantity, 50);
    EXPECT_EQ(buy2->quantity, 50);
}

TEST_F(MatchingTest, DifferentTradersMatch) {
    Order* sell = create_order(Side::Sell, to_price(10.00), 50, 100);
    book.add_order(sell);

    Order* buy = create_order(Side::Buy, to_price(10.00), 50, 200);
    auto trades = book.match(buy);

    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 50);
    EXPECT_EQ(trades[0].price, to_price(10.00));

    EXPECT_FALSE(book.has_order(sell->order_id));
    EXPECT_FALSE(book.has_order(buy->order_id));
}

TEST_F(MatchingTest, SelfMatchPreventionMultiLevel) {
    Order* sell1 = create_order(Side::Sell, to_price(9.00), 50, 100);
    Order* sell2 = create_order(Side::Sell, to_price(10.00), 50, 100);
    Order* sell3 = create_order(Side::Sell, to_price(11.00), 50, 100);
    book.add_order(sell1);
    book.add_order(sell2);
    book.add_order(sell3);

    // Buy@12 from the same trader as all three resting sells.  Self-match
    // prevention skips every ask level.  Resting the buy would create a
    // crossed book (best bid $12 > best ask $9), so the engine cancels it.
    Order* buy = create_order(Side::Buy, to_price(12.00), 150, 100);
    auto trades = book.match(buy);

    EXPECT_EQ(trades.size(), 0);

    // Buy must NOT be in the book.
    EXPECT_FALSE(book.has_order(buy->order_id));

    // All resting sell orders are untouched.
    EXPECT_TRUE(book.has_order(sell1->order_id));
    EXPECT_TRUE(book.has_order(sell2->order_id));
    EXPECT_TRUE(book.has_order(sell3->order_id));
}

TEST_F(MatchingTest, EmptyBookMatching) {
    Order* buy = create_order(Side::Buy, to_price(10.00), 100, 100);
    auto trades = book.match(buy);

    EXPECT_EQ(trades.size(), 0);

    EXPECT_TRUE(book.has_order(buy->order_id));
    EXPECT_EQ(buy->quantity, 100);
}

TEST_F(MatchingTest, SingleSidedBookBidsOnly) {
    Order* buy1 = create_order(Side::Buy, to_price(9.00), 50, 100);
    Order* buy2 = create_order(Side::Buy, to_price(10.00), 50, 100);
    book.add_order(buy1);
    book.add_order(buy2);

    Order* buy3 = create_order(Side::Buy, to_price(11.00), 100, 200);
    auto trades = book.match(buy3);

    EXPECT_EQ(trades.size(), 0);

    EXPECT_TRUE(book.has_order(buy1->order_id));
    EXPECT_TRUE(book.has_order(buy2->order_id));
    EXPECT_TRUE(book.has_order(buy3->order_id));
}

TEST_F(MatchingTest, SingleSidedBookAsksOnly) {
    Order* sell1 = create_order(Side::Sell, to_price(10.00), 50, 100);
    Order* sell2 = create_order(Side::Sell, to_price(11.00), 50, 100);
    book.add_order(sell1);
    book.add_order(sell2);

    Order* sell3 = create_order(Side::Sell, to_price(9.00), 100, 200);
    auto trades = book.match(sell3);

    EXPECT_EQ(trades.size(), 0);

    EXPECT_TRUE(book.has_order(sell1->order_id));
    EXPECT_TRUE(book.has_order(sell2->order_id));
    EXPECT_TRUE(book.has_order(sell3->order_id));
}

TEST_F(MatchingTest, SweepMultipleLevels) {
    Order* sell1 = create_order(Side::Sell, to_price(9.00), 100, 100);
    Order* sell2 = create_order(Side::Sell, to_price(10.00), 100, 200);
    Order* sell3 = create_order(Side::Sell, to_price(11.00), 100, 300);
    book.add_order(sell1);
    book.add_order(sell2);
    book.add_order(sell3);

    Order* buy = create_order(Side::Buy, to_price(11.00), 300, 400);
    auto trades = book.match(buy);

    EXPECT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].quantity, 100);
    EXPECT_EQ(trades[0].price, to_price(9.00));
    EXPECT_EQ(trades[1].quantity, 100);
    EXPECT_EQ(trades[1].price, to_price(10.00));
    EXPECT_EQ(trades[2].quantity, 100);
    EXPECT_EQ(trades[2].price, to_price(11.00));

    EXPECT_FALSE(book.has_order(sell1->order_id));
    EXPECT_FALSE(book.has_order(sell2->order_id));
    EXPECT_FALSE(book.has_order(sell3->order_id));

    EXPECT_FALSE(book.has_order(buy->order_id));

    EXPECT_FALSE(book.get_best_bid().valid);
    EXPECT_FALSE(book.get_best_ask().valid);
}
