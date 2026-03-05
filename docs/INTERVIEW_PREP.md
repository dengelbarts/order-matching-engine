# Interview Talking Points — Order Matching Engine

## Elevator Pitch (30 seconds)

"I built a production-quality limit order book in C++17 over 25 days. It handles limit,
market, IOC, and FOK orders with price-time priority, processes 172K+ orders per second
sustained through a lock-free SPSC queue, uses a memory pool for zero-allocation hot path,
and includes a FIX-like message parser and full market data API. 195 tests, AddressSanitizer,
ThreadSanitizer, and UndefinedBehaviorSanitizer clean, GitHub Actions CI on Ubuntu GCC,
Ubuntu Clang, and macOS."

---

## Core Design Decisions

### 1. Why `std::map` for the order book sides instead of `unordered_map`?

Price levels must be iterated in price order during matching — best bid downward, best ask
upward. `std::map` gives O(log N) insert/erase and in-order iteration for free. An
`unordered_map` would require sorting at match time. In practice the number of distinct price
levels is small (hundreds, not millions), so the log factor is negligible vs the constant cost
of sorting on every match.

### 2. Why fixed-point `int64_t` for prices instead of `double`?

Floating-point arithmetic is non-associative and accumulates rounding error — catastrophic in
financial systems. `int64_t` with 4 implied decimal places gives exact arithmetic, exact
equality comparisons, and consistent behavior across platforms. The cost is a small conversion
layer (`to_price()` / `to_double()`).

### 3. Why a slab `ObjectPool` instead of `new`/`delete` per order?

`new`/`delete` under load causes heap contention, fragmentation, and non-deterministic
latency spikes. The pool pre-allocates a contiguous slab and maintains a free-list stack —
O(1) alloc and free, zero heap traffic in the steady state. The `Order` struct is 64 bytes
(one cache line) so pool slots are cache-line aligned and adjacent orders don't share lines.

### 4. Why a lock-free SPSC queue between producer and matching threads?

A mutex would serialize the producer and matching thread, throttling throughput and adding
kernel-mode overhead on every order. The SPSC queue is wait-free on both ends — the producer
spins only when the ring is full. False sharing is eliminated by placing head and tail on
separate cache lines (`alignas(64) PaddedIndex`). Capacity is a power of 2 so modulo reduces
to a bitmask with no division in the hot path.

### 5. How does self-match prevention work?

Each order carries a `trader_id`. During `match_impl`, if the incoming order's `trader_id`
equals the resting order's `trader_id`, that resting order is skipped. If all available
quantity at the best price belongs to the same trader, the incoming order is cancelled (never
rests) to avoid a crossed book — consistent with CME, Nasdaq, and ICE exchange behavior.

### 6. How does IOC differ from FOK?

- **IOC**: fill as much as available at the limit price or better, cancel any unfilled
  remainder immediately. Partial fills are allowed.
- **FOK**: before touching the book, do a read-only `can_fill()` walk to check if the full
  quantity can be filled. If not, cancel immediately with zero trades. All-or-nothing.

### 7. Amendment priority rules — why does a qty decrease keep priority but a qty increase loses it?

This mirrors exchange practice (NASDAQ, ICE). A quantity decrease is a concession — the
trader is giving up queue position they already held, so keeping their time priority is fair.
A quantity increase is a new economic commitment — they are effectively adding to the book and
must go to the back of the queue at that price level, just like a new order.

---

## Performance Numbers (memorize these)

| Metric | Result | Target |
|--------|--------|--------|
| Sustained throughput (1M orders) | **180K orders/s** | ≥ 150K/s ✅ |
| Limit add mean latency | **126 ns** | < 5 µs ✅ |
| Limit add p99 latency | **173 ns** | < 10 µs ✅ |
| IOC match mean latency | **1,367 ns** | < 5 µs ✅ |
| IOC match p99 latency | **1,638 ns** | < 10 µs ✅ |
| 15-min soak test | **5.87B ops, 0 violations** | — ✅ |
| Pool heap fallbacks | **0** | 0 ✅ |

Build: GCC 13.3, `-O3`, Release, single core.

---

## Architecture Walk-Through (for whiteboard)

```
Producer Thread
    │ FIX-like string ("NEW|side=BUY|price=10.50|qty=100")
    ▼
FIX Parser  ──► ParsedMessage (string_view, zero-copy)
    │
    ▼
OrderCommand factory  ──► 64-byte cache-aligned struct
    │
    ▼
SpscQueue<OrderCommand, 65536>  (wait-free ring buffer)
    │  try_pop() spins
    ▼
Matching Thread ──► OrderBook::create_order()
    │                   │
    │              ObjectPool<Order>  (O(1) alloc, slab)
    │                   │
    │              match_impl()  (price-time priority walk)
    │                   │
    │              Callbacks: TradeEvent, OrderEvent
    │
    ▼
Market Data API
    get_bbo()       ── best bid & ask in one call
    get_depth(n)    ── top N levels per side
    get_snapshot()  ── full book
```

---

## Test Coverage (195 tests)

| Area | Tests | Sanitizers |
|------|-------|------------|
| Price & enums | 7 | ASan, UBSan |
| Order struct | 11 | ASan, UBSan |
| PriceLevel FIFO | 11 | ASan, UBSan |
| OrderBook (add/cancel/BBO) | 28 | ASan, UBSan |
| Limit matching | 9 | ASan, UBSan |
| Edge cases & self-match | 8 | ASan, UBSan |
| Event system | 8 | ASan, UBSan |
| Market orders | 8 | ASan, UBSan |
| IOC orders | 9 | ASan, UBSan |
| FOK orders | 10 | ASan, UBSan |
| Order amendments | 9 | ASan, UBSan |
| Phase 2 integration | 11 | ASan, UBSan |
| ObjectPool | 10 | ASan, UBSan |
| SPSC queue (incl. concurrent 1M) | 8 | TSan |
| MatchingPipeline (incl. 100K) | 7 | TSan |
| Market data API | 15 | ASan, UBSan |
| FIX parser | 22 | ASan, UBSan |

---

## Common Follow-Up Questions

**Q: How would you scale this to multiple symbols?**
One `OrderBook` per symbol. The `MatchingPipeline` can be templated or hold a `symbol_id`
routing table. Multiple pipelines run in parallel — no shared state between books.

**Q: How would you add persistence / crash recovery?**
Write-ahead log (WAL): before processing each `OrderCommand`, append it to a memory-mapped
file. On restart, replay the log to reconstruct book state. The SPSC queue already serialises
commands so the log is inherently ordered.

**Q: What's the bottleneck at higher load?**
At 1M+ orders/s the `std::map` tree node allocations become the bottleneck when many new
price levels are created. The next optimization would be a pool-backed `std::pmr::map` using
`std::pmr::monotonic_buffer_resource`, eliminating all dynamic allocation from the hot path.

**Q: Why not use a hash map with a sorted vector for the book?**
A sorted `std::vector<PriceLevel>` would give better cache locality for iteration but O(N)
insert/delete when price levels are created or destroyed. For a book with hundreds of active
levels and frequent level creation (volatile markets), `std::map` is the better trade-off.
For a narrow, stable spread this could be revisited.

**Q: Is the SPSC queue safe to use from multiple producers?**
No — SPSC means Single Producer, Single Consumer. Multiple producers would require either a
mutex around `try_push()` or an MPSC (Multi-Producer Single Consumer) queue. The
`MatchingPipeline` documents this constraint explicitly.
