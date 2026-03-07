#include "fix42_parser.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helper: builds a valid FIX 4.2 message from a list of body tag=value pairs.
// Prepends 8= and 9=, appends 10= with a correct checksum.
// ---------------------------------------------------------------------------
static std::string build_fix(const std::vector<std::pair<int, std::string>>& fields) {
    // Build body.
    std::string body;
    for (auto& [tag, value] : fields) {
        body += std::to_string(tag) + "=" + value + "\x01";
    }

    // Header.
    std::string msg = "8=FIX.4.2\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;

    // Checksum.
    uint32_t chk = 0;
    for (unsigned char c : msg)
        chk += c;
    chk %= 256;

    char buf[4];
    buf[0] = static_cast<char>('0' + (chk / 100));
    buf[1] = static_cast<char>('0' + (chk % 100 / 10));
    buf[2] = static_cast<char>('0' + (chk % 10));
    msg += "10=";
    msg.append(buf, 3);
    msg += "\x01";
    return msg;
}

// Corrupt a computed checksum string.
static std::string corrupt_checksum(std::string msg) {
    // Find "10=" near the end and flip last digit of checksum value.
    auto pos = msg.rfind("10=");
    if (pos == std::string::npos)
        return msg;
    // Move to first digit of checksum.
    pos += 3;
    // Find the SOH after checksum value.
    auto soh = msg.find('\x01', pos);
    if (soh == std::string::npos)
        return msg;
    // Flip the last digit.
    msg[soh - 1] = (msg[soh - 1] == '0') ? '1' : '0';
    return msg;
}

static Fix42Parser parser;

// ---------------------------------------------------------------------------
// Valid messages
// ---------------------------------------------------------------------------

TEST(Fix42Parser, ValidNewOrderSingleBuy) {
    auto msg = build_fix({
        {35, "D"}, {49, "CLIENT"}, {56, "ENGINE"}, {34, "1"},
        {11, "CLORD001"}, {55, "AAPL"}, {54, "1"}, {38, "100"}, {40, "2"}, {44, "150.5000"}
    });
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.type, Fix42MsgType::NewOrderSingle);
    EXPECT_EQ(r.cl_ord_id, "CLORD001");
    EXPECT_EQ(r.symbol, "AAPL");
    EXPECT_EQ(r.side, Side::Buy);
    EXPECT_EQ(r.qty, 100u);
    EXPECT_EQ(r.ord_type, OrderType::Limit);
    EXPECT_EQ(r.price, to_price(150.5));
}

TEST(Fix42Parser, ValidNewOrderSingleSell) {
    auto msg = build_fix({
        {35, "D"}, {49, "CLIENT"}, {56, "ENGINE"}, {34, "2"},
        {11, "CLORD002"}, {55, "AAPL"}, {54, "2"}, {38, "50"}, {40, "2"}, {44, "149.0000"}
    });
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.side, Side::Sell);
    EXPECT_EQ(r.qty, 50u);
    EXPECT_EQ(r.price, to_price(149.0));
}

TEST(Fix42Parser, FieldOrderIndependence) {
    // Fields in non-standard order — parser must handle any order.
    auto msg = build_fix({
        {55, "MSFT"}, {38, "200"}, {54, "1"}, {44, "300.0000"},
        {40, "2"}, {11, "CLORD003"}, {49, "C"}, {56, "E"}, {34, "1"}, {35, "D"}
    });
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.type, Fix42MsgType::NewOrderSingle);
    EXPECT_EQ(r.symbol, "MSFT");
    EXPECT_EQ(r.qty, 200u);
}

TEST(Fix42Parser, MarketOrder) {
    // OrdType=1 (Market), no price required.
    auto msg = build_fix({
        {35, "D"}, {49, "C"}, {56, "E"}, {34, "1"},
        {11, "MKT001"}, {55, "AAPL"}, {54, "1"}, {38, "10"}, {40, "1"}
    });
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.ord_type, OrderType::Market);
}

TEST(Fix42Parser, IOCOrder) {
    auto msg = build_fix({
        {35, "D"}, {49, "C"}, {56, "E"}, {34, "1"},
        {11, "IOC001"}, {55, "AAPL"}, {54, "2"}, {38, "30"}, {40, "3"}, {44, "100.0000"}
    });
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.ord_type, OrderType::IOC);
}

TEST(Fix42Parser, FOKOrder) {
    auto msg = build_fix({
        {35, "D"}, {49, "C"}, {56, "E"}, {34, "1"},
        {11, "FOK001"}, {55, "AAPL"}, {54, "1"}, {38, "75"}, {40, "4"}, {44, "200.0000"}
    });
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.ord_type, OrderType::FOK);
}

TEST(Fix42Parser, ValidOrderCancelRequest) {
    auto msg = build_fix({
        {35, "F"}, {49, "C"}, {56, "E"}, {34, "2"},
        {11, "CLORD002"}, {41, "CLORD001"}, {55, "AAPL"}, {54, "1"}
    });
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.type, Fix42MsgType::OrderCancelRequest);
    EXPECT_EQ(r.cl_ord_id, "CLORD002");
    EXPECT_EQ(r.orig_cl_ord_id, "CLORD001");
}

TEST(Fix42Parser, ValidOrderCancelReplaceRequest) {
    auto msg = build_fix({
        {35, "G"}, {49, "C"}, {56, "E"}, {34, "3"},
        {11, "CLORD003"}, {41, "CLORD001"}, {55, "AAPL"}, {54, "1"}, {38, "200"}, {44, "155.0000"}
    });
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.type, Fix42MsgType::OrderCancelReplaceRequest);
    EXPECT_EQ(r.qty, 200u);
    EXPECT_EQ(r.price, to_price(155.0));
}

TEST(Fix42Parser, ValidLogon) {
    auto msg = build_fix({
        {35, "A"}, {49, "CLIENT"}, {56, "ENGINE"}, {34, "1"}, {108, "30"}
    });
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.type, Fix42MsgType::Logon);
    EXPECT_EQ(r.heartbeat_int, 30u);
    EXPECT_EQ(r.sender_comp_id, "CLIENT");
    EXPECT_EQ(r.target_comp_id, "ENGINE");
}

TEST(Fix42Parser, ValidLogout) {
    auto msg = build_fix({
        {35, "5"}, {49, "CLIENT"}, {56, "ENGINE"}, {34, "5"}
    });
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid) << r.error;
    EXPECT_EQ(r.type, Fix42MsgType::Logout);
}

// ---------------------------------------------------------------------------
// Invalid messages
// ---------------------------------------------------------------------------

TEST(Fix42Parser, BadChecksum) {
    auto msg = corrupt_checksum(build_fix({
        {35, "D"}, {49, "C"}, {56, "E"}, {34, "1"},
        {11, "X"}, {55, "AAPL"}, {54, "1"}, {38, "1"}, {40, "2"}, {44, "100.0000"}
    }));
    auto r = parser.parse(msg);
    EXPECT_FALSE(r.valid);
    EXPECT_STREQ(r.error, "checksum mismatch");
}

TEST(Fix42Parser, WrongBodyLength) {
    // Start from a valid message, then inflate the stated body length by 5.
    auto valid = build_fix({
        {35, "D"}, {49, "C"}, {56, "E"}, {34, "1"},
        {11, "X"}, {55, "AAPL"}, {54, "1"}, {38, "1"}, {40, "2"}, {44, "100.0000"}
    });

    // Replace "9=N\x01" with "9=<N+5>\x01".
    auto tag9 = valid.find("9=");
    auto soh9 = valid.find('\x01', tag9);
    std::string stated_len_str = valid.substr(tag9 + 2, soh9 - tag9 - 2);
    size_t new_len = std::stoul(stated_len_str) + 5;
    std::string tampered =
        valid.substr(0, tag9) + "9=" + std::to_string(new_len) + valid.substr(soh9);

    // Recompute checksum so the parser reaches the body length check.
    auto ck_pos = tampered.rfind("10=");
    uint32_t chk = 0;
    for (size_t i = 0; i < ck_pos; ++i)
        chk += static_cast<unsigned char>(tampered[i]);
    chk %= 256;
    char buf[3];
    buf[0] = static_cast<char>('0' + (chk / 100));
    buf[1] = static_cast<char>('0' + (chk % 100 / 10));
    buf[2] = static_cast<char>('0' + (chk % 10));
    tampered = tampered.substr(0, ck_pos + 3) + std::string(buf, 3) + "\x01";

    auto r = parser.parse(tampered);
    EXPECT_FALSE(r.valid);
    EXPECT_STREQ(r.error, "BodyLength mismatch");
}

TEST(Fix42Parser, MissingSymbol) {
    auto msg = build_fix({
        {35, "D"}, {49, "C"}, {56, "E"}, {34, "1"},
        {11, "X"}, {54, "1"}, {38, "10"}, {40, "2"}, {44, "100.0000"}
        // no tag 55
    });
    auto r = parser.parse(msg);
    EXPECT_FALSE(r.valid);
    EXPECT_NE(r.error, nullptr);
}

TEST(Fix42Parser, MissingClOrdId) {
    auto msg = build_fix({
        {35, "D"}, {49, "C"}, {56, "E"}, {34, "1"},
        {55, "AAPL"}, {54, "1"}, {38, "10"}, {40, "2"}, {44, "100.0000"}
        // no tag 11
    });
    auto r = parser.parse(msg);
    EXPECT_FALSE(r.valid);
}

TEST(Fix42Parser, UnknownMsgType) {
    auto msg = build_fix({
        {35, "Z"}, {49, "C"}, {56, "E"}, {34, "1"}
    });
    auto r = parser.parse(msg);
    EXPECT_FALSE(r.valid);
    EXPECT_STREQ(r.error, "unknown MsgType (tag 35)");
}

TEST(Fix42Parser, EmptyMessage) {
    auto r = parser.parse("");
    EXPECT_FALSE(r.valid);
    EXPECT_STREQ(r.error, "empty message");
}

TEST(Fix42Parser, MissingOrigClOrdIdForCancel) {
    auto msg = build_fix({
        {35, "F"}, {49, "C"}, {56, "E"}, {34, "1"},
        {11, "CLORD002"}, {55, "AAPL"}, {54, "1"}
        // no tag 41
    });
    auto r = parser.parse(msg);
    EXPECT_FALSE(r.valid);
}

// ---------------------------------------------------------------------------
// Side and OrdType mapping
// ---------------------------------------------------------------------------

TEST(Fix42Parser, SideMapping) {
    auto buy_msg = build_fix({
        {35, "D"}, {49, "C"}, {56, "E"}, {34, "1"},
        {11, "X"}, {55, "S"}, {54, "1"}, {38, "1"}, {40, "2"}, {44, "1.0000"}
    });
    EXPECT_EQ(parser.parse(buy_msg).side, Side::Buy);

    auto sell_msg = build_fix({
        {35, "D"}, {49, "C"}, {56, "E"}, {34, "1"},
        {11, "X"}, {55, "S"}, {54, "2"}, {38, "1"}, {40, "2"}, {44, "1.0000"}
    });
    EXPECT_EQ(parser.parse(sell_msg).side, Side::Sell);
}

TEST(Fix42Parser, OrdTypeMapping) {
    auto limit_msg = build_fix({
        {35, "D"}, {49, "C"}, {56, "E"}, {34, "1"},
        {11, "X"}, {55, "S"}, {54, "1"}, {38, "1"}, {40, "2"}, {44, "1.0000"}
    });
    EXPECT_EQ(parser.parse(limit_msg).ord_type, OrderType::Limit);

    auto mkt_msg = build_fix({
        {35, "D"}, {49, "C"}, {56, "E"}, {34, "1"},
        {11, "X"}, {55, "S"}, {54, "1"}, {38, "1"}, {40, "1"}
    });
    EXPECT_EQ(parser.parse(mkt_msg).ord_type, OrderType::Market);
}

// ---------------------------------------------------------------------------
// to_order_command round-trip
// ---------------------------------------------------------------------------

TEST(Fix42Parser, ToOrderCommandNewOrder) {
    auto msg = build_fix({
        {35, "D"}, {49, "C"}, {56, "E"}, {34, "1"},
        {11, "CLORD001"}, {55, "AAPL"}, {54, "2"}, {38, "500"}, {40, "2"}, {44, "99.5000"}
    });
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid);

    auto cmd = parser.to_order_command(r, 42);
    EXPECT_EQ(cmd.type, CommandType::NewOrder);
    EXPECT_EQ(cmd.symbol_id, 42u);
    EXPECT_EQ(cmd.side(), Side::Sell);
    EXPECT_EQ(cmd.quantity, 500u);
    EXPECT_EQ(cmd.price, to_price(99.5));
    EXPECT_EQ(cmd.order_type(), OrderType::Limit);
}

TEST(Fix42Parser, ToOrderCommandCancel) {
    auto msg = build_fix({
        {35, "F"}, {49, "C"}, {56, "E"}, {34, "1"},
        {11, "CLORD002"}, {41, "12345"}, {55, "AAPL"}, {54, "1"}
    });
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid);

    auto cmd = parser.to_order_command(r, 1);
    EXPECT_EQ(cmd.type, CommandType::Cancel);
    EXPECT_EQ(cmd.order_id, 12345u);
}

TEST(Fix42Parser, ToOrderCommandAmend) {
    auto msg = build_fix({
        {35, "G"}, {49, "C"}, {56, "E"}, {34, "1"},
        {11, "CLORD003"}, {41, "99"}, {55, "AAPL"}, {54, "1"}, {38, "250"}, {44, "110.0000"}
    });
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid);

    auto cmd = parser.to_order_command(r, 1);
    EXPECT_EQ(cmd.type, CommandType::Amend);
    EXPECT_EQ(cmd.order_id, 99u);
    EXPECT_EQ(cmd.new_qty, 250u);
    EXPECT_EQ(cmd.new_price, to_price(110.0));
}
