# Order Matching Engine

A high-performance limit order matching engine implemented in modern C++17.

## Status

🚧 **Work in Progress** - Phase 1 complete, implementing Phase 2: Extended Order Types

### Implementation Progress

- [x] Phase 1: Core Matching Engine (Days 1-10) — `v0.1.0-core`
  - [x] Day 1: Project Setup & Build System
  - [x] Day 2: Price Representation & Core Enums
  - [x] Day 3: Order Struct
  - [x] Day 4: PriceLevel Class
  - [x] Day 5: OrderBook Data Structures & Add Order
  - [x] Day 6: OrderBook Cancel & Best Bid/Offer (BBO)
  - [x] Day 7: Limit Order Matching Engine
  - [x] Day 8: Multi-level Matching & Edge Cases
  - [x] Day 9: Trade Output & Event System
  - [x] Day 10: Phase 1 Integration & Review (86 tests, ASan clean, DESIGN.md)
- [ ] Phase 2: Extended Order Types (Days 11-15)
  - [ ] Day 11: Market Orders
  - [ ] Day 12: IOC (Immediate or Cancel) Orders
  - [ ] Day 13: FOK (Fill or Kill) Orders
  - [ ] Day 14: Order Amendments
  - [ ] Day 15: Phase 2 Integration & Review
- [ ] Phase 3: Performance Optimization (Days 16-20)
  - [ ] Day 16: Baseline Benchmarks
  - [ ] Day 17: Memory Pool (ObjectPool)
  - [ ] Day 18: Hot-Path Optimization
  - [ ] Day 19: Realistic Benchmark Suite
  - [ ] Day 20: Performance Polish & Documentation
- [ ] Phase 4: Multithreading & Final Polish (Days 21-25)
  - [ ] Day 21: SPSC Lock-Free Queue
  - [ ] Day 22: Producer-Consumer Threading
  - [ ] Day 23: Market Data API & FIX Parser
  - [ ] Day 24: README, CI & Documentation
  - [ ] Day 25: Final Review & Ship (v1.0.0)

## Features (Planned)

- Price-time priority matching
- Order types: Limit, Market, IOC, FOK
- Order amendments and cancellations
- Lock-free SPSC queue for multi-threading
- Memory pooling for zero-allocation hot path
- Target: 150,000+ orders/second throughput

## Building
```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Run tests
cd build && ctest --output-on-failure

# Run main executable
./build/ome_main
```

## Requirements

- CMake 3.14+
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- Google Test (fetched automatically)

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Documentation

See [docs/DESIGN.md](docs/DESIGN.md) for architectural decisions and implementation details.

## Development Timeline

This project follows a 25-day structured implementation plan. Each day's work is tagged for easy reference and code review.

### Git Tag Index

| Tag | Date | Description | Status |
|-----|------|-------------|--------|
| [`day-1`](../../tree/day-1) | Feb 9, 2026 | Project setup & build system | ✅ Complete |
| [`day-2`](../../tree/day-2) | Feb 10, 2026 | Price representation & core enums | ✅ Complete |
| [`day-3`](../../tree/day-3) | Feb 11, 2026 | Order struct | ✅ Complete |
| [`day-4`](../../tree/day-4) | Feb 12, 2026 | PriceLevel class | ✅ Complete |
| [`day-5`](../../tree/day-5) | Feb 13, 2026 | OrderBook data structures | ✅ Complete |
| [`day-6`](../../tree/day-6) | Feb 14, 2026 | OrderBook cancel & BBO | ✅ Complete |
| [`day-7`](../../tree/day-7) | Feb 15, 2026 | Limit order matching | ✅ Complete |
| [`day-8`](../../tree/day-8) | Feb 16, 2026 | Multi-level matching & edge cases | ✅ Complete |
| [`day-9`](../../tree/day-9) | Feb 17, 2026 | Trade output & event system | ✅ Complete |
| `day-10` | Feb 18, 2026 | **Phase 1 complete** | ⏳ Planned |
| | | |
| **Milestone** | | [`v0.1.0-core`](../../tree/v0.1.0-core) | Phase 1: Core matching engine |
| `day-11` | Feb 19, 2026 | Market orders | ⏳ Planned |
| `day-12` | Feb 20, 2026 | IOC orders | ⏳ Planned |
| `day-13` | Feb 21, 2026 | FOK orders | ⏳ Planned |
| `day-14` | Feb 22, 2026 | Order amendments | ⏳ Planned |
| `day-15` | Feb 23, 2026 | **Phase 2 complete** | ⏳ Planned |
| | | |
| **Milestone** | | [`v0.2.0-extended`](../../tree/v0.2.0-extended) | Phase 2: Extended order types |
| `day-16` | Feb 24, 2026 | Baseline benchmarks | ⏳ Planned |
| `day-17` | Feb 25, 2026 | Memory pool | ⏳ Planned |
| `day-18` | Feb 26, 2026 | Hot-path optimization | ⏳ Planned |
| `day-19` | Feb 27, 2026 | Realistic benchmarks | ⏳ Planned |
| `day-20` | Feb 28, 2026 | **Phase 3 complete** | ⏳ Planned |
| | | |
| **Milestone** | | [`v0.3.0-performance`](../../tree/v0.3.0-performance) | Phase 3: Performance optimization |
| `day-21` | Mar 1, 2026 | SPSC lock-free queue | ⏳ Planned |
| `day-22` | Mar 2, 2026 | Producer-consumer threading | ⏳ Planned |
| `day-23` | Mar 3, 2026 | Market data API | ⏳ Planned |
| `day-24` | Mar 4, 2026 | CI & documentation | ⏳ Planned |
| `day-25` | Mar 5, 2026 | **Final release** | ⏳ Planned |
| | | |
| **Milestone** | | [`v1.0.0`](../../tree/v1.0.0) | 🚀 Production-ready order matching engine |

### Quick Navigation

```bash
# View code at any point in development
git checkout day-2    # See Day 2: Price implementation
git checkout day-10   # See Phase 1 completion
git checkout v1.0.0   # See final version

# Compare progress between days
git diff day-1..day-2
git diff day-5..day-10

# Return to latest
git checkout main
```

### Daily Progress Details

<details>
<summary><b>Day 1:</b> Project Setup & Build System</summary>

- ✅ GitHub repository structure
- ✅ CMake build system (C++17)
- ✅ Compiler flags: `-Wall -Wextra -Wpedantic -Werror`
- ✅ Google Test integration
- ✅ Directory structure: `/include`, `/src`, `/test`
- ✅ Hello-world compiles successfully
</details>

<details>
<summary><b>Day 2:</b> Price Representation & Core Enums</summary>

- ✅ Price type: `int64_t` fixed-point (4 decimal places)
- ✅ Conversion functions: `to_price()`, `to_double()`, `price_to_string()`
- ✅ `enum class Side { Buy, Sell }`
- ✅ `enum class OrderType { Limit, Market, IOC, FOK }`
- ✅ Comprehensive test suite: 7/7 tests passing
- ✅ Edge cases: negatives, rounding, precision
</details>

<details>
<summary><b>Day 3:</b> Order Struct</summary>

- ✅ Order struct with all required fields (order_id, symbol_id, side, price, quantity, timestamp, order_type)
- ✅ Cache-line optimized: `sizeof(Order) <= 64 bytes`
- ✅ OrderId generator: atomic counter for unique, monotonic IDs
- ✅ Timestamp helper: nanosecond precision using `std::chrono::steady_clock`
- ✅ Debug output operator `operator<<` for Order
- ✅ Comprehensive test suite: 11 tests passing
- ✅ Thread-safety verified for ID generation
</details>

<details>
<summary><b>Day 4:</b> PriceLevel Class</summary>

- ✅ PriceLevel class using `std::deque<Order*>` for FIFO ordering
- ✅ `add_order()`: O(1) append to back
- ✅ `remove_order(OrderId)`: find and erase by ID
- ✅ `get_total_quantity()`: aggregate quantity at price level
- ✅ `front()`: access best time-priority order
- ✅ `is_empty()` and `order_count()`: state queries
- ✅ Comprehensive test suite: 11 tests passing
- ✅ FIFO ordering verified, all edge cases covered
- ✅ Total tests: 33 (all passing)
</details>

<details>
<summary><b>Day 5:</b> OrderBook Data Structures & Add Order</summary>

- ✅ OrderBook class with dual-sided order storage
- ✅ Bid side: `std::map<Price, PriceLevel, std::greater<Price>>` (descending order)
- ✅ Ask side: `std::map<Price, PriceLevel, std::less<Price>>` (ascending order)
- ✅ Order lookup: `std::unordered_map<OrderId, Order*>` for O(1) access
- ✅ `add_order()`: insert into correct side and price level
- ✅ Automatic PriceLevel creation when price not in book
- ✅ `get_order()` and `has_order()`: fast order retrieval by ID
- ✅ Comprehensive test suite: 8 tests passing
  - Add single buy/sell orders
  - Add multiple orders at same price (FIFO verified)
  - Add orders on both sides
  - Bid side sorted descending (best bid first)
  - Ask side sorted ascending (best ask first)
  - Order retrieval by ID
  - Non-existent order handling
- ✅ Total tests: 41 (all passing)
</details>

<details>
<summary><b>Day 6:</b> OrderBook Cancel & Best Bid/Offer (BBO)</summary>

- ✅ `cancel_order(OrderId)`: remove orders from book and lookup map
- ✅ Ghost level cleanup: automatically remove empty price levels after cancel
- ✅ `get_best_bid()`: returns highest bid price + aggregated quantity
- ✅ `get_best_ask()`: returns lowest ask price + aggregated quantity
- ✅ `get_spread()`: calculates bid-ask spread with validity checking
- ✅ BBO struct with `valid` flag for empty book handling
- ✅ Spread struct with `valid` flag for one-sided book handling
- ✅ Comprehensive test suite: 20 new tests passing
  - Cancel existing/non-existent orders
  - Cancel and verify only target order removed
  - Ghost level cleanup verification
  - BBO on empty/single-order/multi-level books
  - BBO quantity aggregation at same price
  - BBO updates after cancellations
  - Spread calculation: empty book, one-sided, tight/wide markets
  - Spread updates after cancel
- ✅ Total tests: 61 (all passing)
</details>

<details>
<summary><b>Day 7:</b> Limit Order Matching Engine</summary>

- ✅ Trade struct with `TradeId` generator (atomic counter)
- ✅ Core `match()` function implementing price-time priority
- ✅ Buy order matching: walk asks from lowest price upward
- ✅ Sell order matching: walk bids from highest price downward
- ✅ Exact fills: orders fully matched and removed from book
- ✅ Partial fills: orders partially matched, remainder rests in book
- ✅ Price improvement: trades execute at resting order's price
- ✅ Multi-level matching: orders sweep through multiple price levels
- ✅ FIFO ordering: earliest orders at each price level match first
- ✅ Automatic cleanup: fully filled orders removed from lookup map
- ✅ Comprehensive test suite: 9 new tests passing
  - ExactMatch: buy/sell at same price fully fills both orders
  - PartialFillBuyRemains: buy fills partially, 50 shares remain in book
  - PartialFillSellRemains: sell fills partially, remainder rests
  - PriceImprovement: buy@10.50 matches sell@10.00, trades at $10.00
  - NoMatch: non-crossing orders (buy@9.00 vs sell@10.00) both rest
  - SellMatchesAgainstBid: sell order matching logic verified
  - FIFOOrdering: first order at price level matches first
  - MultiLevelMatching: buy sweeps 2 ask levels ($9.50, $10.00)
  - MultiLevelMatchingSell: sell sweeps 3 bid levels ($10.50, $10.00, $9.50)
- ✅ **Total tests: 70 (all passing)**
- ✅ **Core matching engine now functional!** 🎯
</details>

<details>
<summary><b>Day 8:</b> Multi-level Matching & Edge Cases</summary>

- ✅ Added `TraderId` field to Order struct (separate from `symbol_id`)
- ✅ Self-match prevention: orders with same `trader_id` won't match
- ✅ Industry-standard implementation (follows CME, Nasdaq, ICE practices)
- ✅ Multi-level sweeping: verified orders sweep through multiple price levels
- ✅ Edge case handling: empty book, single-sided book behaviors
- ✅ Comprehensive test suite: 8 new tests passing
  - SelfMatchPreventionBuy: buy order doesn't match own sells
  - SelfMatchPreventionSell: sell order doesn't match own buys
  - DifferentTradersMatch: different traders can match (normal behavior)
  - SelfMatchPreventionMultiLevel: self-match prevention across price levels
  - EmptyBookMatching: no crashes on empty book
  - SingleSidedBookBidsOnly: handles all-bid books safely
  - SingleSidedBookAsksOnly: handles all-ask books safely
  - SweepMultipleLevels: order sweeps 3 levels (300 qty total)
- ✅ **Total tests: 78 (all passing)**
- ✅ **Self-match prevention complete!** 🛡️
</details>

<details>
<summary><b>Day 9:</b> Trade Output & Event System</summary>

- ✅ `OrderEventType` enum: `New`, `Cancelled`, `Filled`, `PartialFill`
- ✅ `OrderEvent` struct: captures order lifecycle events with full context (original_qty, filled_qty, remaining_qty)
- ✅ `TradeEvent` struct: mirrors Trade for callback-based consumption
- ✅ Human-readable `operator<<` for both event types
- ✅ Callback mechanism: `set_trade_callback()` and `set_order_callback()` via `std::function`
- ✅ `add_order()` fires `OrderEventType::New` on every resting order
- ✅ `cancel_order()` fires `OrderEventType::Cancelled`
- ✅ `match()` fires `TradeEvent` + `Filled`/`PartialFill` per resting order consumed, then `Filled`/`PartialFill` for the incoming order
- ✅ Event ordering guaranteed: resting order event fires before incoming order event
- ✅ `Stats` tracker: `total_orders`, `total_trades`, `total_volume`
- ✅ Integration test: 22-order scenario verifying all event counts and statistics
- ✅ Comprehensive test suite: 8 new tests passing
  - AddOrderFiresNewEvent: New event with correct fields
  - CancelOrderFiresCancelledEvent: Cancelled event on cancel
  - ExactMatchFiresTradeAndFillEvents: 2 Filled events + 1 TradeEvent
  - PartialFillIncomingOrderEvents: Filled + PartialFill + New for remainder
  - NoMatchOnlyNewEvent: no trade events when prices don't cross
  - StatsTracking: counters accurate after mixed order sequence
  - EventOrderAddMatchTrade: resting order event precedes incoming order event
  - IntegrationTest22Orders: 22 orders, 15 trades, 1500 volume, all counts verified
- ✅ **Total tests: 86 (all passing)**
- ✅ **Event system complete — order book is fully observable!** 📡
</details>

---

**Timeline:** 25 days (Feb 9 - Mar 5, 2026)
**Target:** Production-ready order matching engine for portfolio/interviews