#pragma once

#include <string_view>
#include "order.hpp"
#include "order_types.hpp"

enum class FIXMsgType { New, Cancel, Amend, Unknown };

struct ParsedMessage
{
    FIXMsgType type = FIXMsgType::Unknown;

    OrderId id = 0;
    Side side = Side::Buy;
    Price price = 0;
    Quantity qty = 0;

    bool has_id = false;
    bool has_side = false;
    bool has_price = false;
    bool has_qty = false;

    bool valid = false;
    const char *error = nullptr;
};

ParsedMessage parse_fix_message(std::string_view msg);
