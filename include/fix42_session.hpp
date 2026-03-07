#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

// Fix42Session holds per-connection FIX 4.2 session state.
// No I/O — pure state management.
struct Fix42Session {
    std::string sender_comp_id;
    std::string target_comp_id;
    std::atomic<uint32_t> msg_seq_num{1}; // outbound, auto-incremented
    uint32_t expected_seq_num = 1;        // inbound, updated on each received message
    bool logged_in = false;

    Fix42Session() = default;

    Fix42Session(std::string sender, std::string target)
        : sender_comp_id(std::move(sender)), target_comp_id(std::move(target)) {}

    // Non-copyable: atomic member.
    Fix42Session(const Fix42Session&) = delete;
    Fix42Session& operator=(const Fix42Session&) = delete;

    // Returns current outbound seq num and increments for next use.
    uint32_t next_seq_num() {
        return msg_seq_num.fetch_add(1, std::memory_order_relaxed);
    }

    // Validates that an inbound message's sender/target/seq match session state.
    // msg_sender should equal our target; msg_target should equal our sender.
    bool validate_header(std::string_view msg_sender,
                         std::string_view msg_target,
                         uint32_t msg_seq) const {
        if (msg_sender != target_comp_id) return false;
        if (msg_target != sender_comp_id) return false;
        return msg_seq >= expected_seq_num;
    }
};
