#include "fix_parser.hpp"

#include <gtest/gtest.h>

// --- NEW ---

TEST(FIXParser, NewBuyOrder) {
    auto msg = parse_fix_message("NEW|side=BUY|price=10.50|qty=100");
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.type, FIXMsgType::New);
    EXPECT_EQ(msg.side, Side::Buy);
    EXPECT_EQ(msg.price, to_price(10.50));
    EXPECT_EQ(msg.qty, 100u);
}

TEST(FIXParser, NewSellOrder) {
    auto msg = parse_fix_message("NEW|side=SELL|price=11.25|qty=500");
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.side, Side::Sell);
    EXPECT_EQ(msg.price, to_price(11.25));
    EXPECT_EQ(msg.qty, 500u);
}

TEST(FIXParser, NewFieldsInAnyOrder) {
    auto msg = parse_fix_message("NEW|qty=50|price=9.00|side=BUY");
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.side, Side::Buy);
    EXPECT_EQ(msg.qty, 50u);
}

// --- CANCEL ---

TEST(FIXParser, CancelOrder) {
    auto msg = parse_fix_message("CANCEL|id=42");
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.type, FIXMsgType::Cancel);
    EXPECT_EQ(msg.id, 42u);
}

// --- AMEND ---

TEST(FIXParser, AmendQtyAndPrice) {
    auto msg = parse_fix_message("AMEND|id=7|qty=200|price=11.00");
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.type, FIXMsgType::Amend);
    EXPECT_EQ(msg.id, 7u);
    EXPECT_EQ(msg.qty, 200u);
    EXPECT_EQ(msg.price, to_price(11.00));
}

TEST(FIXParser, AmendQtyOnly) {
    auto msg = parse_fix_message("AMEND|id=3|qty=150");
    EXPECT_TRUE(msg.valid);
    EXPECT_TRUE(msg.has_qty);
    EXPECT_FALSE(msg.has_price);
}

TEST(FIXParser, AmendPriceOnly) {
    auto msg = parse_fix_message("AMEND|id=3|price=12.00");
    EXPECT_TRUE(msg.valid);
    EXPECT_FALSE(msg.has_qty);
    EXPECT_TRUE(msg.has_price);
}

// --- Malformed input ---

TEST(FIXParser, EmptyMessage) {
    auto msg = parse_fix_message("");
    EXPECT_FALSE(msg.valid);
    EXPECT_NE(msg.error, nullptr);
}

TEST(FIXParser, UnknownMessageType) {
    auto msg = parse_fix_message("MODIFY|id=1|qty=100");
    EXPECT_FALSE(msg.valid);
    EXPECT_STREQ(msg.error, "unknown message type");
}

TEST(FIXParser, NewMissingSide) {
    auto msg = parse_fix_message("NEW|price=10.00|qty=100");
    EXPECT_FALSE(msg.valid);
    EXPECT_STREQ(msg.error, "missing side");
}

TEST(FIXParser, NewMissingPrice) {
    auto msg = parse_fix_message("NEW|side=BUY|qty=100");
    EXPECT_FALSE(msg.valid);
    EXPECT_STREQ(msg.error, "missing price");
}

TEST(FIXParser, NewMissingQty) {
    auto msg = parse_fix_message("NEW|side=BUY|price=10.00");
    EXPECT_FALSE(msg.valid);
    EXPECT_STREQ(msg.error, "missing qty");
}

TEST(FIXParser, NewZeroQty) {
    auto msg = parse_fix_message("NEW|side=BUY|price=10.00|qty=0");
    EXPECT_FALSE(msg.valid);
    EXPECT_STREQ(msg.error, "qty must be > 0");
}

TEST(FIXParser, NewZeroPrice) {
    auto msg = parse_fix_message("NEW|side=BUY|price=0|qty=100");
    EXPECT_FALSE(msg.valid);
    EXPECT_STREQ(msg.error, "price must be > 0");
}

TEST(FIXParser, CancelMissingId) {
    auto msg = parse_fix_message("CANCEL|qty=100");
    EXPECT_FALSE(msg.valid);
    EXPECT_STREQ(msg.error, "missing id");
}

TEST(FIXParser, AmendMissingId) {
    auto msg = parse_fix_message("AMEND|qty=100");
    EXPECT_FALSE(msg.valid);
    EXPECT_STREQ(msg.error, "missing id");
}

TEST(FIXParser, AmendNoUpdateFields) {
    auto msg = parse_fix_message("AMEND|id=5");
    EXPECT_FALSE(msg.valid);
    EXPECT_STREQ(msg.error, "amend requires qty or price");
}

TEST(FIXParser, InvalidSideValue) {
    auto msg = parse_fix_message("NEW|side=LONG|price=10.00|qty=100");
    EXPECT_FALSE(msg.valid);
    EXPECT_STREQ(msg.error, "invalid side value");
}

TEST(FIXParser, InvalidPrice) {
    auto msg = parse_fix_message("NEW|side=BUY|price=abc|qty=100");
    EXPECT_FALSE(msg.valid);
    EXPECT_STREQ(msg.error, "invalid price");
}

TEST(FIXParser, InvalidQty) {
    auto msg = parse_fix_message("NEW|side=BUY|price=10.00|qty=abc");
    EXPECT_FALSE(msg.valid);
    EXPECT_STREQ(msg.error, "invalid qty");
}

TEST(FIXParser, UnknownField) {
    auto msg = parse_fix_message("NEW|side=BUY|price=10.00|qty=100|symbol=AAPL");
    EXPECT_FALSE(msg.valid);
    EXPECT_STREQ(msg.error, "unknown field");
}

TEST(FIXParser, MalformedKeyValuePair) {
    auto msg = parse_fix_message("NEW|BUY|price=10.00|qty=100");
    EXPECT_FALSE(msg.valid);
    EXPECT_STREQ(msg.error, "malformed key-value pair");
}
