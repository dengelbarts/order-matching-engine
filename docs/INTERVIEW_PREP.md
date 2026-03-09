# Interview Talking Points — Order Matching Engine

## Elevator Pitch (30 seconds)

"I built a complete exchange from scratch in C++17 — FIX 4.2 protocol, multi-symbol routing,
lock-free producer-consumer pipeline, zero-allocation hot path with pmr::map and an object
pool, and a TCP server you can connect to right now and submit real orders. 256 tests,
AddressSanitizer, ThreadSanitizer, and UndefinedBehaviorSanitizer clean, GitHub Actions CI
on Ubuntu GCC, Ubuntu Clang, and macOS including a live end-to-end gateway self-test."

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

### 7. Why `std::pmr::map` with `unsynchronized_pool_resource` instead of `monotonic_buffer_resource`?

`monotonic_buffer_resource` never frees memory — it's for short-lived arenas. An order book
is long-lived and price levels are constantly created and destroyed. `unsynchronized_pool_resource`
maintains per-size free-lists so released tree nodes are recycled, keeping memory bounded.
`unsynchronized` is correct here because the book is accessed only from the matching thread.

### 8. Why epoll instead of select or poll for the TCP server?

`select` and `poll` are O(N) per call — they scan all registered fds. `epoll` is O(1) for
`epoll_wait` (the kernel maintains the ready list internally) and O(log N) for
`epoll_ctl` (fd registration). For a gateway with many concurrent client connections, `epoll`
is the standard Linux choice. Edge-triggered mode (`EPOLLET`) was considered but
level-triggered is simpler to reason about with partial reads and is sufficient for this load.

### 9. Amendment priority rules — why does a qty decrease keep priority but a qty increase loses it?

This mirrors exchange practice (NASDAQ, ICE). A quantity decrease is a concession — the
trader is giving up queue position they already held, so keeping their time priority is fair.
A quantity increase is a new economic commitment — they are effectively adding to the book and
must go to the back of the queue at that price level, just like a new order.

---

## Performance Numbers (memorize these)

| Metric | Result | Target |
|--------|--------|--------|
| Sustained throughput (1M orders) | **1.83M orders/s** | ≥ 150K/s ✅ |
| Multi-symbol throughput (4 symbols, 1M) | **2.11M orders/s** | — ✅ |
| Cancel-heavy throughput | **4.52M orders/s** | — ✅ |
| Limit add mean / p99 latency | **91 ns / 166 ns** | < 5 µs / < 10 µs ✅ |
| IOC match mean / p99 latency | **435 ns / 851 ns** | < 5 µs / < 10 µs ✅ |
| 60-s soak test | **5.77M ops/s, 0 violations** | — ✅ |
| Pool heap fallbacks | **0** | 0 ✅ |

Build: GCC 13.3, `-O3`, Release, single core.

---

## Architecture Walk-Through (for whiteboard)

```
TCP Client (FIX 4.2)
    │ raw bytes over TCP
    ▼
TcpServer  (epoll, non-blocking, accept4)
    │ on_readable(fd)
    ▼
FIXGateway  ──► Fix42Parser (zero-copy string_view, validates checksum)
    │                │
    │           Fix42Session (SenderCompId, SeqNum, logged_in)
    │
    │ OrderCommand (64-byte, cache-aligned)
    ▼
MatchingEngine  ──► symbol_id → OrderBook  (lazy creation)
    │
    ▼
OrderBook::create_order()
    │
    ├── ObjectPool<Order>          (O(1) alloc, zero heap in steady state)
    ├── pmr::map<Price, PriceLevel> (pool-backed, zero tree-node allocs)
    │
    └── match_impl()  (price-time priority walk)
            │
            └── Callbacks: TradeEvent, OrderEvent
                    │
                    ▼
            FIXGateway::push_outbound()
                    │
                    ▼
            Fix42Serializer  ──► ExecutionReport → TCP client
```

---

## Test Coverage (256 tests)

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
| FIX parser (toy) | 22 | ASan, UBSan |
| MatchingEngine (multi-symbol) | 12 | ASan, UBSan |
| FIX 4.2 parser | 15 | ASan, UBSan |
| FIX 4.2 serializer | 10 | ASan, UBSan |
| TcpServer | 8 | TSan |
| FIXGateway (end-to-end) | 6 | TSan |

---

## Common Follow-Up Questions

**Q: How does multi-symbol routing work?**
`MatchingEngine` holds an `unordered_map<SymbolId, OrderBook>`. `route(cmd)` dispatches by
`cmd.symbol_id`, creating the book lazily on first order. Cancel and amend do a reverse lookup
(OrderId → SymbolId) so the caller doesn't need to know which book owns the order. Books are
fully independent — no shared state, no locks between symbols.

**Q: How did you eliminate heap allocations on the hot path?**
Two layers. First, `ObjectPool<Order>` pre-allocates a contiguous slab of `Order` structs and
maintains a free-list stack — O(1) alloc/free, zero `new`/`delete` in steady state. Second,
`OrderBook::bids_` and `asks_` are `std::pmr::map` backed by an
`unsynchronized_pool_resource` — tree node allocations come from the pool, not the heap.
Together, steady-state order processing touches no system allocator.

**Q: How would you add persistence / crash recovery?**
Write-ahead log (WAL): before processing each `OrderCommand`, append it to a memory-mapped
file. On restart, replay the log to reconstruct book state. The SPSC queue already serialises
commands so the log is inherently ordered.

**Q: Why not use a hash map with a sorted vector for the book?**
A sorted `std::vector<PriceLevel>` would give better cache locality for iteration but O(N)
insert/delete when price levels are created or destroyed. For a book with hundreds of active
levels and frequent level creation (volatile markets), `std::map` is the better trade-off.
For a narrow, stable spread this could be revisited.

**Q: Is the SPSC queue safe to use from multiple producers?**
No — SPSC means Single Producer, Single Consumer. Multiple producers would require either a
mutex around `try_push()` or an MPSC (Multi-Producer Single Consumer) queue. The
`MatchingPipeline` documents this constraint explicitly.

**Q: How does the FIX 4.2 gateway handle the matching thread safely?**
`MatchingEngine` callbacks fire on the matching thread. `FIXGateway` runs on the epoll thread.
To avoid locking the matching thread, completed orders are pushed into a lock-free outbound
queue and a pipe byte wakes the epoll loop. The matching thread never blocks — only the
gateway thread does I/O.

**Q: What does FIX 4.2 checksum validation catch?**
The CheckSum (tag 10) is the sum of all bytes before tag 10, mod 256, zero-padded to 3 digits.
It catches single-byte corruption and framing errors. It's not a cryptographic hash — FIX
relies on transport-layer integrity (TCP) for stronger guarantees. The parser also validates
BodyLength (tag 9) and required fields per message type before dispatching.
