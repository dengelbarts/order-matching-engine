#include <gtest/gtest.h>
#include "../include/order_book.hpp"
#include <memory>
#include <vector>

class IntegrationTest : public ::testing::Test
{
    protected:
        OrderBook book;
        std::vector<std::unique_ptr<Order>> orders;
        std::vector<TradeEvent> trades;
        std::vector<OrderEvent> events;
        SymbolId symbol = 1;

        void SetUp() override
        {
            book.set_trade_callback([&](const TradeEvent &e) { trades.push_back(e); });
            book.set_order_callback([&](const OrderEvent &e) { events.push_back(e); });
        }

        Order *make(Side side, Price price, Quantity qty, TraderId trader = 1, OrderType type = OrderType::Limit)
        {
            auto o = std::make_unique<Order>(
                generate_order_id(), symbol, trader, side, price, qty, get_timestamp_ns(), type
            );
            Order *ptr = o.get();
            orders.push_back(std::move(o));
            return ptr;
        }

        Order *make_market(Side side, Quantity qty, TraderId trader = 1)
        {
            return make(side, 0, qty, trader, OrderType::Market);
        }

        Order *make_ioc(Side side, Price price, Quantity qty, TraderId trader = 1)
        {
            return make(side, price, qty, trader, OrderType::IOC);
        }

        Order *make_fok(Side side, Price price, Quantity qty, TraderId trader = 1)
        {
            return make(side, price, qty, trader, OrderType::FOK);
        }
};

TEST_F(IntegrationTest, MixedBookBBOAfterCancels)
{
    Order *b1 = make(Side::Buy, to_price(10.00), 100, 1);
    Order *b2 = make(Side::Buy, to_price(10.00), 50, 2);
    Order *b3 = make(Side::Buy, to_price(9.50), 200, 3);
    Order *a1 = make(Side::Sell, to_price(10.50), 80, 4);
    Order *a2 = make(Side::Sell, to_price(10.50), 120, 5);
    Order *a3 = make(Side::Sell, to_price(11.00), 300, 6);

    for (auto *o : {b1, b2, b3, a1, a2, a3})
        book.add_order(o);

    EXPECT_EQ(book.get_best_bid().price, to_price(10.00));
    EXPECT_EQ(book.get_best_bid().quantity, 150);
    EXPECT_EQ(book.get_best_ask().price, to_price(10.50));
    EXPECT_EQ(book.get_best_ask().quantity, 200);

    book.cancel_order(b1->order_id);
    book.cancel_order(b2->order_id);

    EXPECT_EQ(book.get_best_bid().price, to_price(9.50));
    EXPECT_EQ(book.get_best_bid().quantity, 200);

    book.cancel_order(a1->order_id);
    book.cancel_order(a2->order_id);

    EXPECT_EQ(book.get_best_ask().price, to_price(11.00));
}

TEST_F(IntegrationTest, LimitBuySweepsThreeLevels)
{
    Order *s1 = make(Side::Sell, to_price(10.00), 100, 10);
    Order *s2 = make(Side::Sell, to_price(10.10), 100, 11);
    Order *s3 = make(Side::Sell, to_price(10.20), 100, 12);
    Order *s4 = make(Side::Sell, to_price(10.30), 200, 13);

    for (auto *o : {s1, s2, s3, s4})
        book.add_order(o);

    Order *buy = make(Side::Buy, to_price(10.20), 300, 20);
    auto result = book.match(buy);

    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0].price, to_price(10.00));
    EXPECT_EQ(result[1].price, to_price(10.10));
    EXPECT_EQ(result[2].price, to_price(10.20));

    EXPECT_FALSE(book.has_order(s1->order_id));
    EXPECT_FALSE(book.has_order(s2->order_id));
    EXPECT_FALSE(book.has_order(s3->order_id));
    EXPECT_TRUE(book.has_order(s4->order_id));
    EXPECT_FALSE(book.has_order(buy->order_id));

    EXPECT_EQ(book.get_best_ask().price, to_price(10.30));
}

TEST_F(IntegrationTest, MarketBuyExhaustsBook)
{
    Order *s1 = make(Side::Sell, to_price(10.00), 50, 10);
    Order *s2 = make(Side::Sell, to_price(10.10), 50, 11);
    book.add_order(s1);
    book.add_order(s2);

    Order *mkt = make_market(Side::Buy, 200, 20);
    auto result = book.match(mkt);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].quantity, 50);
    EXPECT_EQ(result[1].quantity, 50);

    EXPECT_FALSE(book.get_best_ask().valid);
    EXPECT_FALSE(book.has_order(mkt->order_id));
}

TEST_F(IntegrationTest, IOCPartialFillThenCancel)
{
    Order *s1 = make(Side::Sell, to_price(10.00), 60, 10);
    book.add_order(s1);

    Order *ioc = make_ioc(Side::Buy, to_price(10.00), 100, 20);
    auto result = book.match(ioc);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].quantity, 60);

    EXPECT_FALSE(book.has_order(ioc->order_id));
    EXPECT_FALSE(book.get_best_bid().valid);
    EXPECT_FALSE(book.get_best_ask().valid);
}

TEST_F(IntegrationTest, FOKKillLeavesBookUnchanged)
{
    Order *s1 = make(Side::Sell, to_price(10.00), 40, 10);
    Order *s2 = make(Side::Sell, to_price(10.10), 40, 11);
    book.add_order(s1);
    book.add_order(s2);

    Order *fok = make_fok(Side::Buy, to_price(10.10), 100, 20);
    auto result = book.match(fok);

    EXPECT_EQ(result.size(), 0);
    EXPECT_TRUE(book.has_order(s1->order_id));
    EXPECT_TRUE(book.has_order(s2->order_id));
    EXPECT_EQ(s1->quantity, 40);
    EXPECT_EQ(s2->quantity, 40);
}

TEST_F(IntegrationTest, FOKFullFillAcrossTwoLevels)
{
    Order *s1 = make(Side::Sell, to_price(10.00), 60, 10);
    Order *s2 = make(Side::Sell, to_price(10.10), 40, 11);
    book.add_order(s1);
    book.add_order(s2);

    Order *fok = make_fok(Side::Buy, to_price(10.10), 100, 20);
    auto result = book.match(fok);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].quantity, 60);
    EXPECT_EQ(result[1].quantity, 40);
    EXPECT_FALSE(book.get_best_ask().valid);
}

TEST_F(IntegrationTest, AmendThenMatch)
{
    Order *sell = make(Side::Sell, to_price(10.00), 100, 10);
    book.add_order(sell);

    Order *buy = make(Side::Buy, to_price(9.50), 100, 20);
    auto r1 = book.match(buy);
    EXPECT_EQ(r1.size(), 0);
    EXPECT_TRUE(book.has_order(buy->order_id));

    book.amend_order(buy->order_id, 100, to_price(10.00));

    Order *sell2 = make(Side::Sell, to_price(10.00), 100, 30);
    auto r2 = book.match(sell2);
    ASSERT_EQ(r2.size(), 1);
    EXPECT_EQ(r2[0].quantity, 100);
    EXPECT_FALSE(book.has_order(buy->order_id));
}

TEST_F(IntegrationTest, AmendQtyDownKeepsPriorityInMatch)
{
    Order *b1 = make(Side::Buy, to_price(10.00), 100, 10);
    Order *b2 = make(Side::Buy, to_price(10.00), 100, 20);
    book.add_order(b1);
    book.add_order(b2);

    book.amend_order(b1->order_id, 50, to_price(10.00));

    Order *sell = make(Side::Sell, to_price(10.00), 60, 30);
    auto result = book.match(sell);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].buy_order_id, b1->order_id);
    EXPECT_EQ(result[0].quantity, 50);
    EXPECT_EQ(result[1].buy_order_id, b2->order_id);
    EXPECT_EQ(result[1].quantity, 10);
}

TEST_F(IntegrationTest, FullMixedScenario)
{
    std::vector<Order*> buys;
    for (int i = 0; i < 10; ++i)
        buys.push_back(make(Side::Buy, to_price(10.00 - i * 0.10), 100, 100 + i));
    for (auto *o : buys) book.add_order(o);

    std::vector<Order*> sells;
    for (int i = 0; i < 10; ++i)
        sells.push_back(make(Side::Sell, to_price(10.50 + i * 0.10), 100, 200 + i));
    for (auto *o : sells) book.add_order(o);

    EXPECT_EQ(book.get_best_bid().price, to_price(10.00));
    EXPECT_EQ(book.get_best_ask().price, to_price(10.50));

    Order *agg_buy = make(Side::Buy, to_price(10.70), 300, 300);
    auto t1 = book.match(agg_buy);
    ASSERT_EQ(t1.size(), 3);
    EXPECT_EQ(book.get_best_ask().price, to_price(10.80));

    Order *mkt_sell = make_market(Side::Sell, 200, 301);
    auto t2 = book.match(mkt_sell);
    ASSERT_EQ(t2.size(), 2);
    EXPECT_EQ(book.get_best_bid().price, to_price(9.80));

    Order *ioc_buy = make_ioc(Side::Buy, to_price(10.80), 150, 302);
    auto t3 = book.match(ioc_buy);
    ASSERT_EQ(t3.size(), 1);
    EXPECT_EQ(t3[0].quantity, 100);
    EXPECT_FALSE(book.has_order(ioc_buy->order_id));

    Order *fok_kill = make_fok(Side::Buy, to_price(10.90), 250, 303);
    auto t4 = book.match(fok_kill);
    EXPECT_EQ(t4.size(), 0);
    EXPECT_FALSE(book.has_order(fok_kill->order_id));

    Order *fok_ok = make_fok(Side::Buy, to_price(10.90), 100, 304);
    auto t5 = book.match(fok_ok);
    ASSERT_EQ(t5.size(), 1);
    EXPECT_EQ(t5[0].quantity, 100);

    Order *resting_bid = buys[2];
    EXPECT_TRUE(book.has_order(resting_bid->order_id));
    book.amend_order(resting_bid->order_id, 50, to_price(9.80));
    EXPECT_EQ(resting_bid->quantity, 50);

    Order *resting_bid2 = buys[3];
    book.amend_order(resting_bid2->order_id, 100, to_price(9.85));
    EXPECT_EQ(resting_bid2->price, to_price(9.85));

    book.cancel_order(buys[4]->order_id);
    book.cancel_order(buys[5]->order_id);
    book.cancel_order(sells[6]->order_id);
    book.cancel_order(sells[7]->order_id);

    EXPECT_FALSE(book.has_order(buys[4]->order_id));
    EXPECT_FALSE(book.has_order(sells[6]->order_id));

    EXPECT_TRUE(book.get_best_bid().valid);
    EXPECT_TRUE(book.get_best_ask().valid);

    size_t total_trades = t1.size() + t2.size() + t3.size() + t4.size() + t5.size();
    EXPECT_GE(total_trades, 7u);

    EXPECT_GE(book.get_stats().total_trades, 7u);
    EXPECT_GE(book.get_stats().total_volume, 700u);
}

TEST_F(IntegrationTest, EventCountOnExactMatch)
{
    Order *sell = make(Side::Sell, to_price(10.00), 100, 10);
    book.add_order(sell);
    events.clear();
    trades.clear();

    Order *buy = make(Side::Buy, to_price(10.00), 100, 20);
    book.match(buy);

    EXPECT_EQ(trades.size(), 1u);

    long filled_count = std::count_if(events.begin(), events.end(), [](const OrderEvent &e) { return e.type == OrderEventType::Filled; });
    EXPECT_EQ(filled_count, 2);
}

TEST_F(IntegrationTest, AmendAndCancelCycle)
{
    for (int i = 0; i < 10; ++i)
    {
        Order *o = make(Side::Buy, to_price(10.00 + i * 0.01), 100, i + 1);
        book.add_order(o);
    }

    for (int i = 0; i < 10; ++i)
    {
        Order *o = orders[i].get();
        if (i % 2 == 0)
            book.amend_order(o->order_id, 50, o->price);
        else
            book.cancel_order(o->order_id);
    }

    EXPECT_EQ(book.get_best_bid().valid, true);
    for (int i = 0; i < 10; ++i)
    {
        Order *o = orders[i].get();
        if (i % 2 == 0)
            EXPECT_TRUE(book.has_order(o->order_id));
        else
            EXPECT_FALSE(book.has_order(o->order_id));
    }
}