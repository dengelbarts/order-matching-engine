#include "fix42_parser.hpp"
#include "fix42_serializer.hpp"

#include <gtest/gtest.h>
#include <string>

// ---------------------------------------------------------------------------
// Helper: compute the expected checksum of a FIX message (bytes before "10=").
// ---------------------------------------------------------------------------
static uint32_t compute_checksum(const std::string& msg) {
    auto pos = msg.rfind("10=");
    // The checksum covers everything up to the \x01 that precedes "10=",
    // i.e. everything before "10=" (that \x01 is included in the bytes before 10=).
    // find "\x01" "10=" to locate the preceding SOH.
    auto ck_pos = msg.rfind("\x01" "10=");
    size_t end = (ck_pos != std::string::npos) ? ck_pos + 1 : 0;
    // If "10=" is the very first thing, end=0 and sum is 0. In practice it's never first.
    (void)pos;
    uint32_t chk = 0;
    for (size_t i = 0; i < end; ++i)
        chk += static_cast<unsigned char>(msg[i]);
    return chk % 256;
}

// Extract the stated checksum value from a FIX message.
static uint32_t stated_checksum(const std::string& msg) {
    auto pos = msg.rfind("10=");
    if (pos == std::string::npos)
        return 999; // sentinel for "not found"
    pos += 3; // skip "10="
    auto soh = msg.find('\x01', pos);
    if (soh == std::string::npos)
        return 999;
    std::string val = msg.substr(pos, soh - pos);
    return static_cast<uint32_t>(std::stoul(val));
}

// Extract the stated body length from a FIX message.
static size_t stated_body_length(const std::string& msg) {
    auto pos = msg.find("9=");
    if (pos == std::string::npos)
        return 0;
    pos += 2;
    auto soh = msg.find('\x01', pos);
    if (soh == std::string::npos)
        return 0;
    return static_cast<size_t>(std::stoul(msg.substr(pos, soh - pos)));
}

// Compute the actual body length (bytes from after "9=N\x01" to before "10=").
static size_t actual_body_length(const std::string& msg) {
    // Find end of "9=N\x01"
    auto tag9 = msg.find("9=");
    if (tag9 == std::string::npos)
        return 0;
    auto after9 = msg.find('\x01', tag9);
    if (after9 == std::string::npos)
        return 0;
    size_t body_start = after9 + 1;

    // Find start of "10="
    auto ck_soh = msg.rfind("\x01" "10=");
    size_t body_end = (ck_soh != std::string::npos) ? ck_soh + 1 : msg.size();

    return body_end - body_start;
}

// Shorthand: make an OrderEvent for testing.
static OrderEvent make_event(OrderEventType type,
                              OrderId id,
                              Side side,
                              Price price,
                              Quantity original_qty,
                              Quantity filled_qty,
                              Quantity remaining_qty) {
    OrderEvent e{};
    e.type = type;
    e.order_id = id;
    e.symbol_id = 1;
    e.side = side;
    e.price = price;
    e.original_qty = original_qty;
    e.filled_qty = filled_qty;
    e.remaining_qty = remaining_qty;
    e.timestamp = 0;
    return e;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(Fix42Serializer, NewOrderChecksumCorrect) {
    Fix42Serializer ser;
    auto e = make_event(OrderEventType::New, 1, Side::Buy, to_price(100.0), 100, 0, 100);
    auto msg = ser.execution_report(e, "CLORD001", "AAPL");

    EXPECT_EQ(stated_checksum(msg), compute_checksum(msg))
        << "Checksum mismatch in New ExecutionReport";
}

TEST(Fix42Serializer, NewOrderBodyLengthCorrect) {
    Fix42Serializer ser;
    auto e = make_event(OrderEventType::New, 2, Side::Sell, to_price(50.25), 200, 0, 200);
    auto msg = ser.execution_report(e, "CLORD002", "MSFT");

    EXPECT_EQ(stated_body_length(msg), actual_body_length(msg))
        << "BodyLength mismatch in New ExecutionReport";
}

TEST(Fix42Serializer, PartialFillExecTypeAndStatus) {
    Fix42Serializer ser;
    // 300 ordered, 100 just filled, 200 remaining.
    auto e = make_event(OrderEventType::PartialFill, 3, Side::Buy, to_price(200.0), 300, 100, 200);
    auto msg = ser.execution_report(e, "CLORD003", "AAPL");

    EXPECT_NE(msg.find("150=F\x01"), std::string::npos) << "ExecType should be F for PartialFill";
    EXPECT_NE(msg.find("39=1\x01"), std::string::npos) << "OrdStatus should be 1 for PartialFill";
    EXPECT_NE(msg.find("32=100\x01"), std::string::npos) << "LastQty should be 100";
    EXPECT_NE(msg.find("151=200\x01"), std::string::npos) << "LeavesQty should be 200";
}

TEST(Fix42Serializer, FilledExecTypeAndStatus) {
    Fix42Serializer ser;
    // 50 ordered, 50 filled, 0 remaining.
    auto e = make_event(OrderEventType::Filled, 4, Side::Sell, to_price(99.0), 50, 50, 0);
    auto msg = ser.execution_report(e, "CLORD004", "GOOG");

    EXPECT_NE(msg.find("150=F\x01"), std::string::npos) << "ExecType should be F for Filled";
    EXPECT_NE(msg.find("39=2\x01"), std::string::npos) << "OrdStatus should be 2 for Filled";
    EXPECT_NE(msg.find("151=0\x01"), std::string::npos) << "LeavesQty should be 0";
    EXPECT_NE(msg.find("32=50\x01"), std::string::npos) << "LastQty should be 50";
}

TEST(Fix42Serializer, CancelledExecTypeAndStatus) {
    Fix42Serializer ser;
    auto e = make_event(OrderEventType::Cancelled, 5, Side::Buy, to_price(150.0), 80, 0, 0);
    auto msg = ser.execution_report(e, "CLORD005", "AMZN");

    EXPECT_NE(msg.find("150=4\x01"), std::string::npos) << "ExecType should be 4 for Cancelled";
    EXPECT_NE(msg.find("39=4\x01"), std::string::npos) << "OrdStatus should be 4 for Cancelled";
}

TEST(Fix42Serializer, AmendedExecTypeAndStatus) {
    Fix42Serializer ser;
    auto e = make_event(OrderEventType::Amended, 6, Side::Buy, to_price(120.0), 100, 0, 100);
    auto msg = ser.execution_report(e, "CLORD006", "TSLA");

    EXPECT_NE(msg.find("150=5\x01"), std::string::npos) << "ExecType should be 5 for Amended";
    EXPECT_NE(msg.find("39=5\x01"), std::string::npos) << "OrdStatus should be 5 for Amended";
}

TEST(Fix42Serializer, CumQtyIsOrigMinusRemaining) {
    Fix42Serializer ser;
    // 500 ordered, 150 just filled, 350 remaining. CumQty = 500 - 350 = 150.
    auto e = make_event(OrderEventType::PartialFill, 7, Side::Buy, to_price(10.0), 500, 150, 350);
    auto msg = ser.execution_report(e, "C", "S");

    EXPECT_NE(msg.find("14=150\x01"), std::string::npos) << "CumQty should be 150";
}

TEST(Fix42Serializer, ContainsBeginString) {
    Fix42Serializer ser;
    auto e = make_event(OrderEventType::New, 8, Side::Buy, to_price(1.0), 1, 0, 1);
    auto msg = ser.execution_report(e, "C", "S");

    EXPECT_EQ(msg.substr(0, 10), "8=FIX.4.2\x01") << "Message must start with BeginString";
}

TEST(Fix42Serializer, ExecIdIsUnique) {
    Fix42Serializer ser;
    auto e1 = make_event(OrderEventType::New, 9, Side::Buy, to_price(1.0), 1, 0, 1);
    auto e2 = make_event(OrderEventType::New, 10, Side::Buy, to_price(1.0), 1, 0, 1);
    auto msg1 = ser.execution_report(e1, "C1", "S");
    auto msg2 = ser.execution_report(e2, "C2", "S");

    // Find exec IDs in both messages.
    auto extract_exec_id = [](const std::string& msg) -> std::string {
        auto pos = msg.find("17=");
        if (pos == std::string::npos) return "";
        pos += 3;
        auto soh = msg.find('\x01', pos);
        return msg.substr(pos, soh - pos);
    };
    EXPECT_NE(extract_exec_id(msg1), extract_exec_id(msg2)) << "ExecID must be unique per report";
}

TEST(Fix42Serializer, ParseBackNewOrder) {
    Fix42Serializer ser;
    Fix42Parser parser;

    auto e = make_event(OrderEventType::New, 11, Side::Buy, to_price(75.5), 1000, 0, 1000);
    auto msg = ser.execution_report(e, "CLORD011", "AAPL");

    // The serialized ExecutionReport must be parseable by Fix42Parser.
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid) << "Parse-back failed: " << (r.error ? r.error : "unknown error");
    EXPECT_EQ(r.type, Fix42MsgType::ExecutionReport);
}

TEST(Fix42Serializer, ParseBackChecksumValid) {
    Fix42Serializer ser;
    Fix42Parser parser;

    auto e = make_event(OrderEventType::Filled, 12, Side::Sell, to_price(200.0), 100, 100, 0);
    auto msg = ser.execution_report(e, "CLORD012", "GOOG");

    // Verify parser validates checksum.
    auto r = parser.parse(msg);
    ASSERT_TRUE(r.valid) << (r.error ? r.error : "unknown");
    // Also verify our independent checksum computation agrees.
    EXPECT_EQ(stated_checksum(msg), compute_checksum(msg));
}

TEST(Fix42Serializer, FillReportPartial) {
    Fix42Serializer ser;
    auto msg = ser.fill_report(42, "CLORD042", "AAPL", Side::Buy,
                               /*original_qty=*/1000, /*last_px=*/to_price(100.0),
                               /*last_qty=*/300, /*cum_qty=*/300, /*leaves_qty=*/700,
                               /*is_fully_filled=*/false);

    EXPECT_NE(msg.find("39=1\x01"), std::string::npos) << "OrdStatus should be 1 (PartialFill)";
    EXPECT_NE(msg.find("32=300\x01"), std::string::npos);
    EXPECT_NE(msg.find("14=300\x01"), std::string::npos);
    EXPECT_NE(msg.find("151=700\x01"), std::string::npos);
    EXPECT_EQ(stated_checksum(msg), compute_checksum(msg));
}

TEST(Fix42Serializer, FillReportFull) {
    Fix42Serializer ser;
    auto msg = ser.fill_report(43, "CLORD043", "MSFT", Side::Sell,
                               /*original_qty=*/500, /*last_px=*/to_price(300.0),
                               /*last_qty=*/500, /*cum_qty=*/500, /*leaves_qty=*/0,
                               /*is_fully_filled=*/true);

    EXPECT_NE(msg.find("39=2\x01"), std::string::npos) << "OrdStatus should be 2 (Filled)";
    EXPECT_NE(msg.find("151=0\x01"), std::string::npos);
    EXPECT_EQ(stated_checksum(msg), compute_checksum(msg));
}
