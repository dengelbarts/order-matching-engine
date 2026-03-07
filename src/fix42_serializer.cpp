#include "fix42_serializer.hpp"

#include <charconv>
#include <cstdint>

static constexpr char SOH = '\x01';

// Append "tag=value\x01" where value is a string.
static void append_sv(std::string& buf, int tag, std::string_view value) {
    char tmp[16];
    auto [ptr, ec] = std::to_chars(tmp, tmp + sizeof(tmp), tag);
    buf.append(tmp, ptr);
    buf.push_back('=');
    buf.append(value);
    buf.push_back(SOH);
}

// Append "tag=N\x01" where N is an unsigned 64-bit integer.
static void append_u64(std::string& buf, int tag, uint64_t value) {
    char vtmp[24];
    auto [vptr, vec] = std::to_chars(vtmp, vtmp + sizeof(vtmp), value);
    append_sv(buf, tag, std::string_view(vtmp, vptr - vtmp));
}

// Append "tag=PRICE\x01" where PRICE is a 4-decimal-place decimal string.
static void append_price(std::string& buf, int tag, Price value) {
    append_sv(buf, tag, price_to_string(value));
}

std::string Fix42Serializer::wrap_with_frame(const std::string& body) {
    // Header: "8=FIX.4.2\x019=<body_len>\x01"
    std::string msg;
    msg.reserve(32 + body.size() + 10);
    msg = "8=FIX.4.2";
    msg.push_back(SOH);
    msg.append("9=");
    char len_buf[16];
    auto [lptr, lec] = std::to_chars(len_buf, len_buf + sizeof(len_buf), body.size());
    msg.append(len_buf, lptr);
    msg.push_back(SOH);

    msg.append(body);

    // Compute checksum over everything so far.
    uint32_t chk = 0;
    for (unsigned char c : msg)
        chk += c;
    chk %= 256;

    // Append "10=CCC\x01" (zero-padded to 3 digits).
    msg.append("10=");
    char chk_buf[3];
    chk_buf[0] = static_cast<char>('0' + (chk / 100));
    chk_buf[1] = static_cast<char>('0' + (chk % 100 / 10));
    chk_buf[2] = static_cast<char>('0' + (chk % 10));
    msg.append(chk_buf, 3);
    msg.push_back(SOH);

    return msg;
}

std::string Fix42Serializer::execution_report(const OrderEvent& event,
                                               std::string_view cl_ord_id,
                                               std::string_view symbol,
                                               std::string_view sender,
                                               std::string_view target,
                                               uint32_t seq_num) {
    body_.clear();

    // Standard header (part of body for BodyLength purposes).
    append_sv(body_, 35, "8");
    append_sv(body_, 49, sender);
    append_sv(body_, 56, target);
    append_u64(body_, 34, seq_num);

    // ExecID: unique per execution.
    uint64_t exec_id = exec_id_.fetch_add(1, std::memory_order_relaxed);
    append_u64(body_, 17, exec_id);

    // ExecType (150) and OrdStatus (39).
    const char* exec_type = "0";
    const char* ord_status = "0";
    switch (event.type) {
    case OrderEventType::New:
        exec_type = "0";
        ord_status = "0";
        break;
    case OrderEventType::PartialFill:
        exec_type = "F";
        ord_status = "1";
        break;
    case OrderEventType::Filled:
        exec_type = "F";
        ord_status = "2";
        break;
    case OrderEventType::Cancelled:
        exec_type = "4";
        ord_status = "4";
        break;
    case OrderEventType::Amended:
        exec_type = "5";
        ord_status = "5";
        break;
    }
    append_sv(body_, 150, exec_type);
    append_sv(body_, 39, ord_status);

    // Order identification.
    append_sv(body_, 11, cl_ord_id);
    append_u64(body_, 37, event.order_id);

    // Instrument.
    append_sv(body_, 55, symbol);

    // Side.
    append_sv(body_, 54, event.side == Side::Buy ? "1" : "2");

    // Quantities.
    //   38 OrderQty   = original quantity of the order
    //   32 LastQty    = quantity filled in this specific execution
    //   31 LastPx     = price of this execution
    //   14 CumQty     = total filled = original_qty - remaining_qty
    //   151 LeavesQty = remaining open quantity
    append_u64(body_, 38, event.original_qty);
    append_u64(body_, 32, event.filled_qty);
    append_price(body_, 31, event.price);
    Quantity cum_qty = event.original_qty - event.remaining_qty;
    append_u64(body_, 14, cum_qty);
    append_u64(body_, 151, event.remaining_qty);

    return wrap_with_frame(body_);
}

std::string Fix42Serializer::fill_report(OrderId order_id,
                                          std::string_view cl_ord_id,
                                          std::string_view symbol,
                                          Side side,
                                          Quantity original_qty,
                                          Price last_px,
                                          Quantity last_qty,
                                          Quantity cum_qty,
                                          Quantity leaves_qty,
                                          bool is_fully_filled,
                                          std::string_view sender,
                                          std::string_view target,
                                          uint32_t seq_num) {
    body_.clear();

    append_sv(body_, 35, "8");
    append_sv(body_, 49, sender);
    append_sv(body_, 56, target);
    append_u64(body_, 34, seq_num);

    uint64_t exec_id = exec_id_.fetch_add(1, std::memory_order_relaxed);
    append_u64(body_, 17, exec_id);

    append_sv(body_, 150, "F");
    append_sv(body_, 39, is_fully_filled ? "2" : "1");

    append_sv(body_, 11, cl_ord_id);
    append_u64(body_, 37, order_id);
    append_sv(body_, 55, symbol);
    append_sv(body_, 54, side == Side::Buy ? "1" : "2");

    append_u64(body_, 38, original_qty);
    append_u64(body_, 32, last_qty);
    append_price(body_, 31, last_px);
    append_u64(body_, 14, cum_qty);
    append_u64(body_, 151, leaves_qty);

    return wrap_with_frame(body_);
}
