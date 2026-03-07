#include "fix42_parser.hpp"

#include <charconv>
#include <cstdlib>
#include <cstring>

static constexpr char SOH = '\x01';

// Reads the next SOH-delimited field from [pos, msg.end()) and advances pos.
// Returns the field WITHOUT the trailing SOH.
static std::string_view next_field(std::string_view msg, size_t& pos) {
    size_t start = pos;
    size_t end = msg.find(SOH, pos);
    if (end == std::string_view::npos) {
        pos = msg.size();
        return msg.substr(start);
    }
    pos = end + 1;
    return msg.substr(start, end - start);
}

// Splits "tag=value" into tag number and value string_view.
static bool split_field(std::string_view field, int& tag, std::string_view& value) {
    auto eq = field.find('=');
    if (eq == std::string_view::npos || eq == 0 || eq + 1 >= field.size())
        return false;
    auto [ptr, ec] = std::from_chars(field.data(), field.data() + eq, tag);
    if (ec != std::errc{} || ptr != field.data() + eq)
        return false;
    value = field.substr(eq + 1);
    return true;
}

static bool parse_uint32(std::string_view v, uint32_t& out) {
    auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
    return ec == std::errc{} && ptr == v.data() + v.size();
}

static bool parse_uint64(std::string_view v, uint64_t& out) {
    auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
    return ec == std::errc{} && ptr == v.data() + v.size();
}

static bool parse_price_fix(std::string_view v, Price& out) {
    char buf[32];
    if (v.size() >= sizeof(buf))
        return false;
    std::memcpy(buf, v.data(), v.size());
    buf[v.size()] = '\0';
    char* end_ptr;
    double d = std::strtod(buf, &end_ptr);
    if (end_ptr != buf + v.size())
        return false;
    out = to_price(d);
    return true;
}

Fix42Message Fix42Parser::parse(std::string_view msg) const {
    Fix42Message result;

    if (msg.empty()) {
        result.error = "empty message";
        return result;
    }

    // Locate the checksum field "10=xxx\x01" — must be the last field.
    // Search for "\x0110=" to find the SOH that precedes the checksum tag.
    size_t checksum_pos;
    size_t ck_search = msg.rfind("\x01" "10=");
    if (ck_search == std::string_view::npos) {
        // Edge case: checksum is the very first field (invalid FIX, but handle gracefully).
        if (msg.size() >= 3 && msg.substr(0, 3) == "10=") {
            checksum_pos = 0;
        } else {
            result.error = "missing checksum (tag 10)";
            return result;
        }
    } else {
        checksum_pos = ck_search + 1; // skip the \x01, point at "10=..."
    }

    // Everything before "10=" is used for checksum calculation.
    std::string_view before_checksum = msg.substr(0, checksum_pos);

    // Compute actual checksum.
    uint32_t computed_chk = 0;
    for (unsigned char c : before_checksum)
        computed_chk += c;
    computed_chk %= 256;

    // Parse and validate stated checksum.
    {
        std::string_view ck_field = msg.substr(checksum_pos);
        size_t eq = ck_field.find('=');
        size_t soh = ck_field.find(SOH);
        if (eq == std::string_view::npos || soh == std::string_view::npos || soh <= eq) {
            result.error = "malformed checksum field";
            return result;
        }
        std::string_view ck_val = ck_field.substr(eq + 1, soh - eq - 1);
        uint32_t stated_chk = 0;
        if (!parse_uint32(ck_val, stated_chk)) {
            result.error = "invalid checksum value";
            return result;
        }
        if (computed_chk != stated_chk) {
            result.error = "checksum mismatch";
            return result;
        }
    }

    // Parse fields from before_checksum.
    size_t pos = 0;

    // Tag 8: BeginString — must be first.
    {
        std::string_view f = next_field(before_checksum, pos);
        if (f != "8=FIX.4.2") {
            result.error = "invalid or missing BeginString (tag 8)";
            return result;
        }
    }

    // Tag 9: BodyLength — must be second.
    size_t body_start;
    {
        std::string_view f = next_field(before_checksum, pos);
        int tag_num;
        std::string_view val;
        if (!split_field(f, tag_num, val) || tag_num != 9) {
            result.error = "expected BodyLength (tag 9) as second field";
            return result;
        }
        uint32_t stated_body_len = 0;
        if (!parse_uint32(val, stated_body_len)) {
            result.error = "invalid BodyLength value";
            return result;
        }
        body_start = pos;
        size_t actual_body_len = before_checksum.size() - body_start;
        if (actual_body_len != stated_body_len) {
            result.error = "BodyLength mismatch";
            return result;
        }
    }

    // Parse body fields (pos now at first body field).
    bool has_msg_type = false;

    while (pos < before_checksum.size()) {
        std::string_view f = next_field(before_checksum, pos);
        if (f.empty())
            continue;

        int tag_num;
        std::string_view val;
        if (!split_field(f, tag_num, val)) {
            result.error = "malformed tag=value field";
            return result;
        }

        switch (tag_num) {
        case 8:
            // Duplicate BeginString — ignore (already consumed above, but
            // body_start may include it if it appears in body; skip silently).
            break;
        case 35:
            has_msg_type = true;
            if (val == "A")
                result.type = Fix42MsgType::Logon;
            else if (val == "5")
                result.type = Fix42MsgType::Logout;
            else if (val == "D")
                result.type = Fix42MsgType::NewOrderSingle;
            else if (val == "F")
                result.type = Fix42MsgType::OrderCancelRequest;
            else if (val == "G")
                result.type = Fix42MsgType::OrderCancelReplaceRequest;
            else if (val == "8")
                result.type = Fix42MsgType::ExecutionReport;
            else {
                result.error = "unknown MsgType (tag 35)";
                return result;
            }
            break;
        case 49:
            result.sender_comp_id = val;
            break;
        case 56:
            result.target_comp_id = val;
            break;
        case 34: {
            uint32_t seq = 0;
            if (!parse_uint32(val, seq)) {
                result.error = "invalid MsgSeqNum (tag 34)";
                return result;
            }
            result.msg_seq_num = seq;
            break;
        }
        case 11:
            result.cl_ord_id = val;
            break;
        case 41:
            result.orig_cl_ord_id = val;
            break;
        case 55:
            result.symbol = val;
            break;
        case 54:
            if (val == "1")
                result.side = Side::Buy;
            else if (val == "2")
                result.side = Side::Sell;
            else {
                result.error = "invalid Side (tag 54)";
                return result;
            }
            break;
        case 38:
            if (!parse_uint64(val, result.qty)) {
                result.error = "invalid OrderQty (tag 38)";
                return result;
            }
            break;
        case 40:
            if (val == "1")
                result.ord_type = OrderType::Market;
            else if (val == "2")
                result.ord_type = OrderType::Limit;
            else if (val == "3")
                result.ord_type = OrderType::IOC;
            else if (val == "4")
                result.ord_type = OrderType::FOK;
            else {
                result.error = "invalid OrdType (tag 40)";
                return result;
            }
            break;
        case 44:
            if (!parse_price_fix(val, result.price)) {
                result.error = "invalid Price (tag 44)";
                return result;
            }
            break;
        case 108: {
            uint32_t hbi = 0;
            if (!parse_uint32(val, hbi)) {
                result.error = "invalid HeartBtInt (tag 108)";
                return result;
            }
            result.heartbeat_int = hbi;
            break;
        }
        // ExecutionReport fields
        case 17:
            result.exec_id_sv = val;
            break;
        case 37:
            result.order_id_sv = val;
            break;
        case 150:
            result.exec_type = val;
            break;
        case 39:
            result.ord_status = val;
            break;
        case 32:
            if (!parse_uint64(val, result.last_qty)) {
                result.error = "invalid LastQty (tag 32)";
                return result;
            }
            break;
        case 31:
            if (!parse_price_fix(val, result.last_px)) {
                result.error = "invalid LastPx (tag 31)";
                return result;
            }
            break;
        case 14:
            if (!parse_uint64(val, result.cum_qty)) {
                result.error = "invalid CumQty (tag 14)";
                return result;
            }
            break;
        case 151:
            if (!parse_uint64(val, result.leaves_qty)) {
                result.error = "invalid LeavesQty (tag 151)";
                return result;
            }
            break;
        default:
            // Ignore unknown tags per FIX spec.
            break;
        }
    }

    if (!has_msg_type) {
        result.error = "missing MsgType (tag 35)";
        return result;
    }

    // Per-type required field validation.
    switch (result.type) {
    case Fix42MsgType::Logon:
    case Fix42MsgType::Logout:
    case Fix42MsgType::ExecutionReport:
        break;

    case Fix42MsgType::NewOrderSingle:
        if (result.cl_ord_id.empty()) {
            result.error = "NewOrderSingle missing ClOrdId (tag 11)";
            return result;
        }
        if (result.symbol.empty()) {
            result.error = "NewOrderSingle missing Symbol (tag 55)";
            return result;
        }
        if (result.qty == 0) {
            result.error = "NewOrderSingle missing or zero OrderQty (tag 38)";
            return result;
        }
        if (result.ord_type == OrderType::Limit && result.price == 0) {
            result.error = "Limit NewOrderSingle missing Price (tag 44)";
            return result;
        }
        break;

    case Fix42MsgType::OrderCancelRequest:
        if (result.cl_ord_id.empty()) {
            result.error = "OrderCancelRequest missing ClOrdId (tag 11)";
            return result;
        }
        if (result.orig_cl_ord_id.empty()) {
            result.error = "OrderCancelRequest missing OrigClOrdId (tag 41)";
            return result;
        }
        if (result.symbol.empty()) {
            result.error = "OrderCancelRequest missing Symbol (tag 55)";
            return result;
        }
        break;

    case Fix42MsgType::OrderCancelReplaceRequest:
        if (result.cl_ord_id.empty()) {
            result.error = "OrderCancelReplaceRequest missing ClOrdId (tag 11)";
            return result;
        }
        if (result.orig_cl_ord_id.empty()) {
            result.error = "OrderCancelReplaceRequest missing OrigClOrdId (tag 41)";
            return result;
        }
        if (result.symbol.empty()) {
            result.error = "OrderCancelReplaceRequest missing Symbol (tag 55)";
            return result;
        }
        if (result.qty == 0) {
            result.error = "OrderCancelReplaceRequest missing or zero OrderQty (tag 38)";
            return result;
        }
        break;

    default:
        break;
    }

    result.valid = true;
    return result;
}

OrderCommand Fix42Parser::to_order_command(const Fix42Message& msg, SymbolId sym_id) const {
    switch (msg.type) {
    case Fix42MsgType::NewOrderSingle: {
        OrderId id = generate_order_id();
        return OrderCommand::make_new(
            id, sym_id, 0, msg.side, msg.price, msg.qty, get_timestamp_ns(), msg.ord_type);
    }
    case Fix42MsgType::OrderCancelRequest: {
        OrderId id = 0;
        parse_uint64(msg.orig_cl_ord_id, id);
        return OrderCommand::make_cancel(id);
    }
    case Fix42MsgType::OrderCancelReplaceRequest: {
        OrderId id = 0;
        parse_uint64(msg.orig_cl_ord_id, id);
        return OrderCommand::make_amend(id, msg.price, msg.qty);
    }
    default:
        return OrderCommand::make_shutdown();
    }
}
