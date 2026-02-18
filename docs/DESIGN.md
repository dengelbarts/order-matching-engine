# Order Matching Engine — Design Document

## Phase 1: Core Matching Engine (Days 1–10)

---

## 1. Overview

A single-symbol, in-memory limit order book implemented in C++17.
Price-time priority (FIFO) matching for Limit orders, with a
callback-based event system and AddressSanitizer-clean memory management.

**Phase 1 scope:**
- Fixed-point price representation
- Cache-line-sized Order struct
- PriceLevel (FIFO queue per price point)
- OrderBook with bid/ask maps, add, cancel, BBO, spread
- Limit order matching (price-time priority, multi-level sweep)
- Self-match prevention
- Trade and Order event callbacks
- Basic statistics (total orders, trades, volume)

---

## 2. Data Structures

### 2.1 Price Representation

`Price` is `int64_t` with 4 decimal places of fixed-point precision:

```
stored_value = actual_price × 10,000
```

Example: $10.50 → `105000`

**Why fixed-point?**
- Floating-point accumulates rounding errors across millions of operations.
- Integer arithmetic is exact, branch-predictor-friendly, and fully deterministic.
- 4 decimal places covers equities, FX, and most commodity contracts (0.0001 tick).
- `int64_t` supports prices up to ~$922 trillion — effectively unlimited.

### 2.2 Order Struct

```
Field       Type        Bytes
order_id    uint64_t    8
price       int64_t     8
quantity    uint64_t    8
timestamp   uint64_t    8
symbol_id   uint32_t    4
trader_id   uint32_t    4
side        enum(1B)    1
order_type  enum(1B)    1
padding                 6
TOTAL                  48  (≤ 64, verified by static_assert)
```

**Why 64 bytes?** A cache line on all modern x86/ARM CPUs is 64 bytes.
One Order = one cache-line fetch. No straddling penalty in the matching loop.

### 2.3 PriceLevel

```cpp
class PriceLevel {
    std::deque<Order*> orders_;
};
```

| Operation       | Complexity                     |
|-----------------|--------------------------------|
| add_order       | O(1) amortised                 |
| front           | O(1)                           |
| remove_order    | O(n) — n = orders at level     |
| get_total_qty   | O(n)                           |
| is_empty        | O(1)                           |

**Why `std::deque`?**
`std::deque` provides O(1) front/back access and avoids per-node heap
allocation (unlike `std::list`). For the typical case of a handful of
orders per level, the cache locality of contiguous chunks outperforms
linked lists. Random-access iteration for `remove_order` is also supported
without the overhead of pointer chasing.

**FIFO invariant:** New orders append to the back; matching consumes from
the front. This is the definition of price-time priority.

### 2.4 OrderBook Maps

```cpp
std::map<Price, PriceLevel, std::greater<Price>> bids_;  // descending
std::map<Price, PriceLevel, std::less<Price>>    asks_;  // ascending
std::unordered_map<OrderId, Order*>              order_lookup_;
```

| Operation            | Complexity                          |
|----------------------|-------------------------------------|
| add_order            | O(log P) — P = distinct price levels|
| cancel_order         | O(log P + n)                        |
| get_best_bid/ask     | O(1) — iterator to begin()          |
| get_spread           | O(1)                                |
| match (limit order)  | O(K × n) — K levels swept           |
| lookup by OrderId    | O(1) average                        |

**Why `std::map` over a hash map?**
The matching loop must iterate price levels in sorted order (best price
first). An `unordered_map` doesn't maintain order. A sorted vector would
give O(n) inserts. `std::map` gives O(log P) insert/delete and O(1)
best-price access via `begin()`.

**Why `std::greater<Price>` for bids?**
The best bid is the *highest* price. `std::greater` makes `begin()` yield
the highest key, so `get_best_bid()` is simply `bids_.begin()` — O(1).

---

## 3. Matching Engine

### 3.1 Algorithm

For a **buy** order (aggressor) against the ask side:

```
for each ask level from lowest price upward:
    if ask_price > order_limit_price → stop
    for each order at level (front to back, FIFO):
        if same trader_id → skip entire level (self-match prevention)
        trade_qty = min(remaining_qty, resting.qty)
        emit TradeEvent and fill OrderEvents
        decrement both quantities
        if resting fully filled → remove from level and lookup map
    if level is now empty → erase from asks map
if incoming order has remaining_qty > 0 → rest it in bids map
```

Sell orders mirror this on the bid side (descending order, highest first).

### 3.2 Self-Match Prevention

When the front order at a price level shares a `trader_id` with the
incoming order, the entire level is skipped. This is a conservative
strategy: if a trader has an order anywhere at the front of a level, the
engine will not execute against any order at that level.

**Trade-off:** A more permissive engine could skip only the conflicting
order and continue matching behind it. This was chosen for Phase 1 because
it is simpler, correct, and avoids mutating the deque mid-iteration.

### 3.3 Partial Fills and Book Resting

If an incoming Limit order is partially filled, `match()` calls
`add_order()` internally with the reduced quantity. The order rests in the
book at its original limit price. This maintains FIFO fairness — the order
keeps its original timestamp.

### 3.4 Event Callbacks

```cpp
void set_trade_callback(std::function<void(const TradeEvent&)> cb);
void set_order_callback(std::function<void(const OrderEvent&)> cb);
```

**Firing order within a single `match()` call:**
1. Resting order event (Filled / PartialFill) — per individual trade
2. Trade event — per individual trade
3. Incoming order event (Filled, PartialFill, or New for remainder)

**Why `std::function` over virtual dispatch?**
- Lambdas (including capturing lambdas) compose cleanly with test fixtures.
- No inheritance hierarchy required in the engine headers.
- Overhead of `std::function` is acceptable for event notification (off the
  critical matching path in a production engine, events would be batched).

---

## 4. Memory Model and Ownership

`OrderBook` holds **non-owning** raw pointers to `Order` objects.
Ownership lives with the caller:

```cpp
auto order = std::make_unique<Order>(...);
book.add_order(order.get());   // book borrows; never frees
```

**Why raw pointers in the public interface?**
Raw `T*` in modern C++ means *non-owning borrowed pointer* — the callee
will not free it. `std::shared_ptr` would add atomic reference-count
operations on every add and remove, unacceptable at sub-microsecond
latency targets.

**RAII compliance:**
- `PriceLevel` holds `deque<Order*>` — no ownership, destructs cleanly.
- `OrderBook` destructor releases the maps and unordered_map; it does not
  free any Order objects (by design).
- All test fixtures use `std::unique_ptr<Order>` for safe cleanup.

---

## 5. Build Targets

```bash
# Standard debug build + tests
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

# AddressSanitizer (Phase 1 review requirement)
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure

# ThreadSanitizer (Phase 4 threading)
cmake -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

---

## 6. Test Coverage (Phase 1)

| File                    | Coverage                                           |
|-------------------------|----------------------------------------------------|
| test_price.cpp          | Fixed-point conversions, round-trips, edge cases   |
| test_order.cpp          | Construction, ID uniqueness, monotonic timestamps  |
| test_price_level.cpp    | FIFO ordering, add/remove, quantity tracking       |
| test_order_book.cpp     | Add, cancel, BBO, spread, empty book               |
| test_matching.cpp       | Exact/partial fills, price improvement, multi-level sweep, self-match |
| test_events.cpp         | Callback firing order, stats accuracy, 20-order integration test |

---

## 7. Complexity Summary

| Operation             | Time       | Notes                            |
|-----------------------|------------|----------------------------------|
| Add order             | O(log P)   | Map insert at price level        |
| Cancel order          | O(log P + n) | Map lookup + deque scan        |
| Best bid / ask        | O(1)       | Map begin()                      |
| Spread                | O(1)       | Two begin() calls                |
| Match (limit)         | O(K × n)  | K levels, n orders per level     |
| Order lookup by ID    | O(1) avg   | unordered_map                    |

P = distinct price levels, n = orders per level, K = levels swept by match.

---

## 8. Known Limitations (addressed in later phases)

| Limitation            | Plan                                      |
|-----------------------|-------------------------------------------|
| Limit orders only     | Market, IOC, FOK — Phase 2 (Days 11–14)  |
| No order amendment    | Phase 2 Day 14                            |
| Single-threaded       | SPSC lock-free queue — Phase 3 Day 21–22  |
| No memory pool        | ObjectPool<Order> — Phase 3 Day 17        |
| No FIX-style parser   | Phase 4 Day 23                            |
| std::map for levels   | Profile vs flat_map — Phase 3 Day 18      |

---

## 9. Phase 1 Tag

`v0.1.0-core` — tagged after all Phase 1 tests pass with zero ASan warnings.
