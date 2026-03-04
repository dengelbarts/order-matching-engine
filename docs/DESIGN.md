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

---

## Phase 2: Extended Order Types (Days 11–15)

---

## 10. Order Type Implementations

### 10.1 Market Orders (Day 11)

A Market order has no price constraint — it matches at whatever price is
available. Implementation: temporarily set the price to `INT64_MAX` (buy)
or `0` (sell) before entering the matching loop. This lets the existing
price-comparison logic work unchanged. The sentinel value is restored
before any cancel event is fired.

**Key property:** Any unfilled remainder is cancelled immediately. Market
orders never rest in the book.

### 10.2 IOC — Immediate or Cancel (Day 12)

IOC is a price-constrained Market order. It matches at the limit price or
better, and any unfilled portion is cancelled. The matching loop is
identical to Limit; the difference is what happens after: if
`remaining_qty > 0`, a `Cancelled` event fires and the order is not added
to the book.

**Difference from Market:** IOC respects a limit price. A Market order
ignores price entirely.

### 10.3 FOK — Fill or Kill (Day 13)

FOK requires that the *entire* quantity be fillable before a single trade
executes. Implementation: a read-only `can_fill()` pre-check walks the
opposite side and tallies available quantity. If `can_fill()` returns
false, a `Cancelled` event fires and the loop is never entered — no book
state is modified.

**Key invariant:** `can_fill()` is side-effect-free. The book is never
mutated on a FOK kill.

### 10.4 Order Amendment (Day 14)

`amend_order(order_id, new_qty, new_price)` implements the exchange rule
for time-priority:

| Change | Priority |
|---|---|
| Quantity decrease | Preserved — update in place |
| Quantity increase | Lost — remove and re-add |
| Price change (any) | Lost — remove and re-add |
| Amendment to zero | Treated as cancel |

"Lost priority" means the order is removed from its current position in the
deque and re-appended at the back of the new price level, with a fresh
timestamp. This matches the behaviour of NYSE, LSE, and most venues.

An `Amended` `OrderEvent` fires carrying `old_price` and `old_qty`
alongside the new values, enabling downstream systems to audit the change.

---

## 11. Phase 2 Test Coverage

| File | Coverage |
|---|---|
| test_market_orders.cpp | Full fill, multi-level, partial+cancel, empty book |
| test_ioc_orders.cpp | Full fill, partial fill, no fill, price mismatch |
| test_fok_orders.cpp | Full fill, kill (qty), kill (price), event verification |
| test_amend_orders.cpp | Priority rules, price level migration, event fields |
| test_integration.cpp | 11 mixed scenarios, 50+ orders, final book state |

---

## 12. Phase 2 Tag

`v0.2.0-extended` — tagged after all Phase 2 tests pass with zero ASan warnings.

---

## Phase 3: Performance Optimization (Days 16–20)

---

## 13. ObjectPool\<T, Capacity\>

`ObjectPool<T, Capacity>` (`include/object_pool.hpp`) provides O(1) allocation and deallocation for `Order` objects on the matching hot path.

**Design:**
- Storage: `std::unique_ptr<std::byte[]>` heap slab of `Capacity` objects with `alignas(T)` alignment (moved to heap from stack in Day 19 to support capacities > 4K without stack overflow).
- Free-list: intrusive stack of slot indices — `allocate()` pops an index and calls placement `new`; `deallocate()` calls `ptr->~T()` then pushes the index back.
- Heap fallback: when the pool is exhausted, falls back to `new`/`delete` with a `stderr` warning. This keeps the system functional and makes exhaustion observable without aborting.
- `is_from_pool(ptr)`: pointer bounds check against slab start/end — used in `OrderBook` to guard `deallocate()` calls and avoid freeing stack-allocated test orders.

| Operation      | Complexity | Notes                     |
|----------------|------------|---------------------------|
| allocate()     | O(1)       | Stack pop + placement new |
| deallocate()   | O(1)       | Destructor + stack push   |
| is_from_pool() | O(1)       | Pointer comparison        |

**Why not `std::pmr::pool_resource`?**
PMR is C++17 but adds virtual dispatch and a `memory_resource*` pointer to every allocation. The custom pool is zero-overhead on the hot path and exposes exactly the stats needed for benchmarking.

---

## 14. Hot-Path Optimizations (Day 18)

Three targeted changes to eliminate allocations on the matching hot path:

### 14.1 Scratch vector

`OrderBook` holds a `std::vector<Trade> scratch_trades_` member. Instead of allocating a new vector per `match()` call, the matching loop reuses this vector (cleared at entry). On the optimised pool path (`create_order()`), zero heap allocations occur in steady state.

### 14.2 Branch hints

Self-match prevention is the only conditional on the inner matching loop. Since self-trades are rare in production, `LIKELY`/`UNLIKELY` macros (`__builtin_expect`) guide the branch predictor toward the fast path.

### 14.3 Pool integration in OrderBook

`create_order(args...)` allocates from the pool, calls `match()`, and returns the resting pointer or `nullptr` if the order was fully consumed. `cancel_order()` and `match()` return consumed orders via `pool_.deallocate()` guarded by `pool_.is_from_pool()`.

---

## 15. Benchmark Suite (Days 16–19)

All benchmarks use Google Benchmark (v1.8.3 via FetchContent). The `bench` custom CMake target runs the full suite:

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
cmake --build build/release --target bench
```

| Benchmark | What it measures |
|---|---|
| `BM_ThroughputAddOnly` | Raw add-order throughput, no matching |
| `BM_ThroughputWithPool` | Full optimised path: pool alloc + match + dealloc |
| `BM_SustainedThroughput` | 60% limit / 20% cancel / 10% IOC / 10% amend |
| `BM_CancelHeavy` | Volatile-market: 60% add / 35% cancel / 5% IOC |
| `BM_DeepBook` | Build 1K–10K price levels then sweep with market buy |
| `BM_Latency_AddLimit` | Per-op add latency under load; p50/p95/p99/p99.9 |
| `BM_Latency_IOCMatch` | Per-op IOC match latency under load |

**Results (GCC 13.3, `-O3`, Release, Ubuntu 22.04):**

| Metric | Result | Target |
|---|---|---|
| Sustained throughput (1M orders) | 180K orders/s | ≥ 150K/s |
| Add limit mean latency | 126 ns | < 5µs |
| Add limit p99 latency | 173 ns | < 10µs |
| IOC match mean latency | 1,367 ns | < 5µs |
| IOC match p99 latency | 1,638 ns | < 10µs |

---

## 16. Phase 3 Tag

`v0.3.0-performance` — tagged after all performance targets are met and documented.

---

## Phase 4: Multithreading & Final Polish (Days 21–23)

---

## 17. SpscQueue\<T, Capacity\> (Day 21)

`SpscQueue<T, Capacity>` (`include/spsc_queue.hpp`) is a wait-free, lock-free single-producer single-consumer ring buffer.

**Design:**

```
Producer thread          Consumer thread
    │                         │
    ▼                         ▼
[head_ cache line]      [tail_ cache line]
    │                         │
    └────── shared ring ───────┘
```

- Ring buffer of `Capacity` slots; power-of-2 enforced by `static_assert`.
- Bitmask modulo: `index & (Capacity - 1)` — no division in the hot path.
- `PaddedIndex`: each index occupies a full 64-byte cache line (`alignas(64)` + padding). This eliminates false sharing between the producer (writes `head_`) and consumer (writes `tail_`).
- `try_push()`: producer relaxed-reads its own head, acquire-reads tail, release-writes new head.
- `try_pop()`: consumer relaxed-reads its own tail, acquire-reads head, release-writes new tail.

**Why acquire/release and not seq_cst?**
`seq_cst` adds a full memory fence on x86, which is unnecessary here because producer and consumer only synchronise with each other. Acquire/release gives the correct happens-before guarantee at lower cost.

---

## 18. MatchingPipeline (Day 22)

`MatchingPipeline` (`include/matching_pipeline.hpp`) decouples order submission from matching execution.

**Structure:**

```
Producer thread(s)        MatchingThread
      │                        │
  submit(cmd)             try_pop(cmd)
      │                        │
      └─► SpscQueue ──────────►│
                           OrderBook::add/cancel/amend
                           Event callbacks fire here
```

- `OrderCommand` (`include/order_command.hpp`): exactly 64 bytes (`static_assert` enforced). `Side`/`OrderType` stored as `uint8_t` — default `enum class` is `int` (4 bytes) which would push the struct past 64 bytes. Factory methods: `make_new()`, `make_cancel()`, `make_amend()`, `make_shutdown()`.
- `start()`: spawns the MatchingThread, which busy-spins on `try_pop()`.
- `submit(cmd)`: producer-side; spins until queue has space, then enqueues. Returns as soon as the command is in the queue.
- `shutdown()`: enqueues a Shutdown sentinel (last in FIFO, so all prior commands are processed first), then joins the MatchingThread.
- `processed()`: atomic counter incremented by the MatchingThread after each command — used in tests to synchronise without locks.

**Thread safety:** all `OrderBook` mutations happen exclusively on the MatchingThread. Event callbacks fire on the MatchingThread; callers should not capture shared mutable state in callbacks without external synchronisation.

---

## 19. Market Data API (Day 23)

Three read-only queries added to `OrderBook`:

| Method | Return type | Description |
|---|---|---|
| `get_bbo()` | `MarketBBO{bid, ask}` | Best bid and best ask in one call |
| `get_depth(n)` | `Depth{bids, asks}` | Top N price levels per side |
| `get_snapshot()` | `Depth{bids, asks}` | Full book — all price levels |

Bid levels are always returned **descending** (best first); ask levels always **ascending** (best first). `get_snapshot()` is equivalent to `get_depth(SIZE_MAX)`.

---

## 20. FIX-like Message Parser (Day 23)

`FIXParser` (`include/fix_parser.hpp`, `src/fix_parser.cpp`) parses a text protocol inspired by FIX tag=value encoding.

**Wire format:**

```
NEW|side=BUY|price=10.50|qty=100
NEW|side=SELL|qty=200|price=9.75
CANCEL|id=42
AMEND|id=7|qty=150|price=10.25
```

Fields within a message type may appear in any order.

**Implementation:**
- All parameters are `std::string_view` — the parser never copies the input buffer, avoiding heap allocation on the hot path.
- Returns `ParsedMessage` with `bool valid` and `const char* error` for every failure path.
- Strict validation: unknown fields, missing required fields, zero qty/price, and malformed key=value pairs all set `valid = false`.

**Why `string_view` over `std::string`?**
FIX messages arrive as a contiguous byte buffer from the network layer. Creating `std::string` copies adds heap allocation on every parse. `string_view` provides the same interface with zero-copy semantics.

---

## 21. Phase 4 Test Coverage

| File | Tests | Coverage |
|---|---|---|
| `test_spsc_queue.cpp` | 8 | Empty, single push/pop, FIFO, capacity limit, wrap-around, concurrent 1M items |
| `test_pipeline.cpp` | 7 | Start/shutdown, single order, trade callback, cancel, amend, 100K throughput, clean shutdown |
| `test_market_data.cpp` | 15 | BBO empty/one-sided/both sides, depth top-N, sort order, aggregation, snapshot |
| `test_fix_parser.cpp` | 22 | Valid NEW/CANCEL/AMEND, field-order independence, all invalid input cases |

---

## 22. Final Release Tag

`v1.0.0` — tagged on Day 25 after all 195 tests pass across Debug, Release, ASan, TSan, and UBSan builds with zero warnings.
