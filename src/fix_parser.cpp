#include "../include/fix_parser.hpp"

#include <charconv>
#include <cstdlib>
#include <cstring>

static std::string_view next_token(std::string_view& remaining, char delim) {
    auto pos = remaining.find(delim);
    if (pos == std::string_view::npos) {
        std::string_view token = remaining;
        remaining = {};
        return token;
    }
    std::string_view token = remaining.substr(0, pos);
    remaining = remaining.substr(pos + 1);
    return token;
}

static bool parse_uint64(std::string_view val, uint64_t& out) {
    auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), out);
    return ec == std::errc{} && ptr == val.data() + val.size();
}

static bool parse_price_sv(std::string_view val, Price& out) {
    char buf[32];
    if (val.size() >= sizeof(buf))
        return false;
    std::memcpy(buf, val.data(), val.size());
    buf[val.size()] = '\0';

    char* end_ptr;
    double d = std::strtod(buf, &end_ptr);
    if (end_ptr != buf + val.size())
        return false;

    out = to_price(d);
    return true;
}

ParsedMessage parse_fix_message(std::string_view msg) {
    ParsedMessage result;

    if (msg.empty()) {
        result.error = "empty message";
        return result;
    }

    std::string_view remaining = msg;
    std::string_view type_str = next_token(remaining, '|');

    if (type_str == "NEW")
        result.type = FIXMsgType::New;
    else if (type_str == "CANCEL")
        result.type = FIXMsgType::Cancel;
    else if (type_str == "AMEND")
        result.type = FIXMsgType::Amend;
    else {
        result.error = "unknown message type";
        return result;
    }

    while (!remaining.empty()) {
        std::string_view kv = next_token(remaining, '|');
        auto eq_pos = kv.find('=');
        if (eq_pos == std::string_view::npos || eq_pos == 0 || eq_pos + 1 == kv.size()) {
            result.error = "malformed key-value pair";
            return result;
        }

        std::string_view key = kv.substr(0, eq_pos);
        std::string_view value = kv.substr(eq_pos + 1);

        if (key == "side") {
            if (value == "BUY")
                result.side = Side::Buy;
            else if (value == "SELL")
                result.side = Side::Sell;
            else {
                result.error = "invalid side value";
                return result;
            }
            result.has_side = true;
        } else if (key == "price") {
            if (!parse_price_sv(value, result.price)) {
                result.error = "invalid price";
                return result;
            }
            result.has_price = true;
        } else if (key == "qty") {
            if (!parse_uint64(value, result.qty)) {
                result.error = "invalid qty";
                return result;
            }
            result.has_qty = true;
        } else if (key == "id") {
            if (!parse_uint64(value, result.id)) {
                result.error = "invalid id";
                return result;
            }
            result.has_id = true;
        } else {
            result.error = "unknown field";
            return result;
        }
    }

    switch (result.type) {
    case FIXMsgType::New:
        if (!result.has_side) {
            result.error = "missing side";
            return result;
        }
        if (!result.has_price) {
            result.error = "missing price";
            return result;
        }
        if (!result.has_qty) {
            result.error = "missing qty";
            return result;
        }
        if (result.qty <= 0) {
            result.error = "qty must be > 0";
            return result;
        }
        if (result.price <= 0) {
            result.error = "price must be > 0";
            return result;
        }
        break;

    case FIXMsgType::Cancel:
        if (!result.has_id) {
            result.error = "missing id";
            return result;
        }
        break;

    case FIXMsgType::Amend:
        if (!result.has_id) {
            result.error = "missing id";
            return result;
        }
        if (!result.has_qty && !result.has_price) {
            result.error = "amend requires qty or price";
            return result;
        }
        if (result.has_qty && result.qty == 0) {
            result.error = "qty must be > 0";
            return result;
        }
        if (result.has_price && result.price <= 0) {
            result.error = "price must be > 0";
            return result;
        }
        break;

    default:
        break;
    }

    result.valid = true;
    return result;
}