#pragma once

#include "events.hpp"
#include "price.hpp"

#include <atomic>
#include <string>
#include <string_view>

// Builds outbound FIX 4.2 messages.
// Uses a reusable internal buffer to minimise per-call allocations.
// Thread-safe only for concurrent calls to different instances; a single
// instance should not be used from multiple threads simultaneously.
class Fix42Serializer {
    std::string body_; // reusable body buffer

    std::atomic<uint64_t> exec_id_{1};

    // Wraps a pre-built body string with FIX framing: computes BodyLength,
    // prepends "8=FIX.4.2\x019=N\x01", appends "10=CCC\x01".
    static std::string wrap_with_frame(const std::string& body);

public:
    Fix42Serializer() { body_.reserve(256); }

    // Builds an ExecutionReport (35=8) for any order lifecycle event.
    //
    // FIX field mapping from OrderEvent:
    //   38 OrderQty   = event.original_qty
    //   32 LastQty    = event.filled_qty  (qty of this specific fill; 0 for New/Cancelled)
    //   31 LastPx     = event.price
    //   14 CumQty     = event.original_qty - event.remaining_qty
    //   151 LeavesQty = event.remaining_qty
    std::string execution_report(const OrderEvent& event,
                                 std::string_view cl_ord_id,
                                 std::string_view symbol,
                                 std::string_view sender = "ENGINE",
                                 std::string_view target = "CLIENT",
                                 uint32_t seq_num = 1);

    // Builds a fill ExecutionReport for one side of a trade.
    // Use this to generate fill reports for both the aggressor and passive sides.
    std::string fill_report(OrderId order_id,
                            std::string_view cl_ord_id,
                            std::string_view symbol,
                            Side side,
                            Quantity original_qty,
                            Price last_px,
                            Quantity last_qty,
                            Quantity cum_qty,
                            Quantity leaves_qty,
                            bool is_fully_filled,
                            std::string_view sender = "ENGINE",
                            std::string_view target = "CLIENT",
                            uint32_t seq_num = 1);
};
