#pragma once

#include "order_command.hpp"
#include "order_types.hpp"
#include "price.hpp"

#include <cstdint>
#include <string_view>

enum class Fix42MsgType : uint8_t {
    Logon,                     // 35=A
    Logout,                    // 35=5
    NewOrderSingle,            // 35=D
    OrderCancelRequest,        // 35=F
    OrderCancelReplaceRequest, // 35=G
    ExecutionReport,           // 35=8 (inbound for parse-back validation)
    Unknown
};

// Zero-copy parsed representation of a FIX 4.2 message.
// All string_view fields point into the original input buffer —
// the caller must keep the buffer alive while using Fix42Message.
struct Fix42Message {
    Fix42MsgType type = Fix42MsgType::Unknown;

    // Session header fields
    std::string_view sender_comp_id; // tag 49
    std::string_view target_comp_id; // tag 56
    uint32_t msg_seq_num = 0;        // tag 34

    // Common application fields
    std::string_view cl_ord_id;      // tag 11
    std::string_view orig_cl_ord_id; // tag 41
    std::string_view symbol;         // tag 55
    Side side = Side::Buy;           // tag 54: 1=Buy 2=Sell
    OrderType ord_type = OrderType::Limit; // tag 40: 1=Market 2=Limit 3=IOC 4=FOK
    Price price = 0;                 // tag 44
    Quantity qty = 0;                // tag 38
    uint32_t heartbeat_int = 0;      // tag 108 (Logon only)

    // ExecutionReport fields (populated when parsing 35=8)
    std::string_view order_id_sv;    // tag 37
    std::string_view exec_id_sv;     // tag 17
    std::string_view exec_type;      // tag 150
    std::string_view ord_status;     // tag 39
    Quantity last_qty = 0;           // tag 32
    Price last_px = 0;               // tag 31
    Quantity cum_qty = 0;            // tag 14
    Quantity leaves_qty = 0;         // tag 151

    bool valid = false;
    const char* error = nullptr;
};

class Fix42Parser {
public:
    // Parse a complete FIX 4.2 message (including framing tags 8, 9, 10).
    // Validates checksum and body length. Zero heap allocation for well-formed messages.
    Fix42Message parse(std::string_view msg) const;

    // Convert a parsed application message to an engine OrderCommand.
    // sym_id: the SymbolId that corresponds to msg.symbol (caller resolves the mapping).
    // For Cancel/Amend, the OrderId is parsed from orig_cl_ord_id; if not numeric, returns 0.
    OrderCommand to_order_command(const Fix42Message& msg, SymbolId sym_id) const;
};
