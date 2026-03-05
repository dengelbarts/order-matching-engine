# Post-Program: Order Matching Engine Extensions

Extensions to the v1.0.0 order matching engine, targeting the gaps that matter most for
Geneva trading/fintech interviews.

**Goal:** Transform the library into a running exchange — real FIX protocol, multi-symbol,
network-connected.

---

## Day 26 — Multi-Symbol Engine + Zero-Allocation Completion

**C++ Study**: `std::pmr` — polymorphic memory resources, `unsynchronized_pool_resource`,
how `std::pmr::map` eliminates per-node heap allocations.

### Project: MatchingEngine class

The `OrderBook` is already single-symbol with a `symbol_id` field on every order and
command. This day adds a `MatchingEngine` router on top and completes the zero-allocation
story with `std::pmr::map`.

**`include/matching_engine.hpp`** — new file:
- `MatchingEngine` owns `std::unordered_map<SymbolId, OrderBook>` (one book per symbol)
- `get_or_create_book(SymbolId) -> OrderBook&` — lazily creates books on first order
- `route(const OrderCommand&)` — dispatches to the correct book by `cmd.symbol_id`
- `get_book(SymbolId) -> OrderBook*` — returns nullptr if symbol unknown (for market data queries)
- `symbol_count() -> size_t`

**`include/matching_pipeline.hpp`** — update:
- Replace the bare `OrderBook` member with `MatchingEngine`
- The matching thread now calls `engine_.route(cmd)` instead of `book_.create_order(...)`
- `get_engine() -> const MatchingEngine&` accessor for market data queries

**`include/order_book.hpp`** + **`src/order_book.cpp`** — pmr swap:
- Add private `std::pmr::unsynchronized_pool_resource pool_resource_` member
- Change `bids_` and `asks_` to `std::pmr::map<Price, PriceLevel, std::greater<Price>>`
  and `std::pmr::map<Price, PriceLevel, std::less<Price>>`, constructed with `&pool_resource_`
- Update `get_bids()` / `get_asks()` return types to match
- No behaviour change — same O(log N) operations, same test semantics, zero tree-node heap
  traffic in steady state

**`test/test_matching_engine.cpp`** — new file, 10+ tests:
- `SingleSymbolRouting`: route NewOrder to symbol 1, verify book has order
- `TwoSymbolsIndependent`: orders on symbol 1 and 2 don't interact
- `MatchWithinSymbol`: buy and sell on same symbol generate trade; other symbol unaffected
- `CancelCrossSymbol`: cancel on wrong symbol returns false
- `LazyBookCreation`: `symbol_count()` grows only when new symbol first seen
- `AmendCrossSymbol`: amend on wrong symbol is a no-op
- `MarketDataPerSymbol`: `get_book()` returns correct book for each symbol
- `UnknownSymbolReturnsNullptr`: `get_book()` on unseen symbol returns nullptr
- Pipeline integration: `HundredKOrdersTwoSymbols` — 50K each, verify independent trade counts

**Benchmarks** — update `bench/bench_throughput.cpp`:
- Add `BM_MultiSymbol`: 4 symbols, round-robin routing, 1M orders — compare ops/s vs single-symbol
- Confirm pmr: run with valgrind `--tool=massif` and show zero steady-state heap growth

**Success metrics**:
- All 195 existing tests still pass
- 10+ new multi-symbol tests pass
- `BM_MultiSymbol` within 10% of single-symbol throughput (routing overhead negligible)
- valgrind massif confirms no heap allocations in steady-state hot path
- `static_assert` on `std::pmr::map` allocator type added as documentation

---

## Day 27 — Real FIX 4.2 Protocol

**C++ Study**: FIX protocol fundamentals — tag=value encoding, SOH delimiter (`\x01`), message
structure (BeginString/BodyLength/CheckSum framing), session vs application layer.

### Project: FIX 4.2 parser and ExecutionReport serializer

Replace the pipe-delimited toy parser with a proper FIX 4.2 implementation. This is the
protocol every trading firm in Geneva runs. Even a partial implementation covering the core
order lifecycle is a meaningful differentiator.

**FIX 4.2 message structure**:
```
8=FIX.4.2\x01 9=<body_len>\x01 35=<type>\x01 ... <fields> ... 10=<checksum>\x01
```
- Fields are `tag=value\x01` pairs (SOH = `\x01` delimiter)
- Tag 8 (BeginString), 9 (BodyLength), 35 (MsgType) always appear first
- Tag 10 (CheckSum) always last: sum of all bytes before tag 10, mod 256, zero-padded to 3 digits

**Inbound message types to support**:

| MsgType (35=) | Name | Required tags |
|---|---|---|
| `A` | Logon | 108 (HeartBtInt) |
| `5` | Logout | — |
| `D` | NewOrderSingle | 11 (ClOrdId), 55 (Symbol), 54 (Side), 38 (OrderQty), 40 (OrdType), 44 (Price, if limit) |
| `F` | OrderCancelRequest | 11 (ClOrdId), 41 (OrigClOrdId), 55 (Symbol), 54 (Side) |
| `G` | OrderCancelReplaceRequest | 11 (ClOrdId), 41 (OrigClOrdId), 55 (Symbol), 54 (Side), 38 (OrderQty), 44 (Price) |

Side: `1`=Buy, `2`=Sell. OrdType: `1`=Market, `2`=Limit, `3`=IOC, `4`=FOK.

**Outbound message type**:

| MsgType (35=) | ExecType (150=) | OrdStatus (39=) | Meaning |
|---|---|---|---|
| `8` | `0` | `0` | New — order resting in book |
| `8` | `F` | `1` | PartialFill |
| `8` | `F` | `2` | Filled |
| `8` | `4` | `4` | Cancelled / Killed |
| `8` | `5` | `5` | Replaced (amend accepted) |

ExecutionReport fields: 11 (ClOrdId), 37 (OrderID), 17 (ExecID, atomic counter), 150 (ExecType),
39 (OrdStatus), 55 (Symbol), 54 (Side), 38 (OrderQty), 32 (LastQty), 31 (LastPx),
14 (CumQty), 151 (LeavesQty).

**`include/fix42_parser.hpp`** + **`src/fix42_parser.cpp`** — new files:
- `Fix42Parser` class
- `parse(std::string_view msg) -> Fix42Message` — zero-copy tag extraction using `string_view`
  into the input buffer; no heap allocation for well-formed messages
- `Fix42Message`: `MsgType type`, `std::string_view cl_ord_id`, `std::string_view orig_cl_ord_id`,
  `std::string_view symbol`, `Side side`, `OrderType ord_type`, `Price price`, `Quantity qty`,
  `bool valid`, `const char* error`
- Validates: checksum (tag 10), body length (tag 9), required fields per message type
- `to_order_command(const Fix42Message&, SymbolId sym_id) -> OrderCommand` — bridge to engine

**`include/fix42_serializer.hpp`** + **`src/fix42_serializer.cpp`** — new files:
- `Fix42Serializer` class
- `execution_report(const OrderEvent&, std::string_view cl_ord_id, std::string_view symbol) -> std::string`
- `execution_report_trade(const TradeEvent&, ...) -> std::string`
- Builds complete FIX message: computes BodyLength, appends CheckSum, uses a reusable
  `std::string` buffer member to minimise allocations

**`include/fix42_session.hpp`** — new file (state only, no I/O):
- `Fix42Session` struct: `SenderCompId`, `TargetCompId`, `MsgSeqNum` (outbound counter),
  `ExpectedSeqNum` (inbound), `logged_in` flag
- `next_seq_num() -> uint32_t` — atomic, thread-safe
- `validate_header(const Fix42Message&) -> bool` — checks SenderCompId/TargetCompId/MsgSeqNum

**`test/test_fix42_parser.cpp`** — new file, 15+ tests:
- Valid NewOrderSingle buy/sell, field order independence, Market/Limit/IOC/FOK types
- Valid OrderCancelRequest, OrderCancelReplaceRequest
- Invalid: bad checksum, wrong body length, missing required field per type, unknown MsgType
- Side and OrdType mapping correctness
- `to_order_command` round-trip: `Fix42Message` → `OrderCommand` → fields match

**`test/test_fix42_serializer.cpp`** — new file, 10+ tests:
- ExecutionReport for New, PartialFill, Filled, Cancelled events
- CheckSum computed correctly (validate with independent calculation)
- BodyLength correct
- Parse-back: serialized ExecutionReport can be re-parsed by `Fix42Parser`

**Success metrics**:
- All previous tests pass
- 25+ new FIX 4.2 tests pass, ASan + UBSan clean
- Serialized ExecutionReports pass checksum validation
- `to_order_command` round-trip produces identical fields to input message
- You can explain FIX tag=value encoding and checksum from memory

---

## Day 28 — TCP Session Layer + End-to-End Demo

**C++ Study**: Linux `epoll` — level vs edge-triggered, `EPOLLIN`/`EPOLLOUT`/`EPOLLERR`,
non-blocking I/O with `O_NONBLOCK`, `accept4`, `recv`/`send` partial read/write handling.

### Project: TCP server + running exchange demo

Connect everything: a TCP server accepts FIX 4.2 client connections, routes orders through
`MatchingEngine`, and streams `ExecutionReport`s back. After this day you can run the binary,
connect with a Python script, and demo a live matching engine in an interview.

**`include/tcp_server.hpp`** + **`src/tcp_server.cpp`** — new files:
- `TcpServer` class — owns the `epoll` fd and listening socket
- `listen(uint16_t port)` — binds, listens, sets `SO_REUSEADDR`
- `run()` — event loop: `epoll_wait`, dispatches `on_accept` / `on_readable` / `on_closed`
- Non-blocking: `accept4(..., SOCK_NONBLOCK)`, `O_NONBLOCK` on all client fds
- `stop()` — writes to a self-pipe, breaks `epoll_wait`, joins cleanly

**`include/fix_gateway.hpp`** + **`src/fix_gateway.cpp`** — new files:
- `FIXGateway` owns `TcpServer`, `MatchingEngine`, `Fix42Parser`, `Fix42Serializer`
- `ClientState`: per-fd buffer (`std::string`), `Fix42Session`, `cl_ord_id` → `OrderId` map
- On `on_readable(fd)`: `recv` into buffer, extract complete FIX messages (scan for trailing
  `10=xxx\x01`), parse, dispatch:
  - `35=A` (Logon): validate, send Logon ack, mark session active
  - `35=5` (Logout): send Logout, close fd
  - `35=D/F/G`: call `engine_.route(cmd)`, store `ClOrdId`→`OrderId` mapping
- Callbacks from `MatchingEngine` fire on the matching thread; use a lock-free notification
  (write OrderId to a per-client pipe or use `eventfd`) to wake the gateway thread and send
  the ExecutionReport — keeps `MatchingEngine` callback path lock-free
- `on_closed(fd)`: cancel all resting orders owned by this session

**`src/main.cpp`** — update to full gateway demo:
```
Usage: ./ome_main [--port 9000]
Starts FIX 4.2 gateway on the given port (default 9000).
```

**`demo/fix_client.py`** — new file, simple Python demo client:
- Connects to `localhost:9000`
- Sends Logon, submits 5 buy orders and 5 sell orders (crossing prices to generate trades),
  prints received ExecutionReports, sends Logout
- Usable in interviews: `python3 demo/fix_client.py` shows the engine live

**`test/test_tcp_server.cpp`** — new file, 8+ tests:
- `ListenAndAccept`: server starts, client connects, `on_accept` fires
- `SendAndReceive`: server echoes bytes back, client receives them
- `MultipleClients`: 3 simultaneous clients, all receive independent responses
- `CleanShutdown`: `stop()` drains in-flight data, all client fds closed
- `PartialRead`: message split across two `recv` calls is reassembled correctly

**`test/test_fix_gateway.cpp`** — new file, integration tests:
- `LogonLogout`: client connects, Logon accepted, Logout closes session cleanly
- `NewOrderAcknowledged`: NewOrderSingle → ExecutionReport 35=8/150=0/39=0 received
- `OrderMatch`: crossing buy and sell → both receive fill ExecutionReports
- `CancelOrder`: order placed then cancelled → Cancelled ExecutionReport received
- `AmendOrder`: price amendment → Replaced ExecutionReport, then match if now crossing
- `SessionCancelOnDisconnect`: resting orders cancelled when client drops connection

**Update CI** (`.github/workflows/ci.yml`):
- Add `--port 0` self-test step (server binds ephemeral port, Python client connects, checks
  one full order lifecycle): `python3 demo/fix_client.py --port $PORT --self-test`

**Record a demo with asciinema** — do this last, once everything works:

```bash
pip install asciinema
asciinema rec demo/demo.cast --title "Order Matching Engine — FIX 4.2 live demo"
# In the recording:
#   1. ./build/ome_main &            (start gateway)
#   2. python3 demo/fix_client.py    (submit orders, watch ExecutionReports)
#   3. kill %1                       (clean shutdown)
```

Convert to GIF and embed at the top of the README, right below the CI badge:

```bash
pip install agg   # asciinema GIF converter
agg demo/demo.cast demo/demo.gif
```

In README.md, add after the badge line:
```markdown
![Demo](demo/demo.gif)
```

Keep the recording under 90 seconds. The ideal script: start the server, log on, submit 3
resting sells at different prices, submit an aggressive buy that sweeps two levels, show the
two ExecutionReports with fills, then cancel the remaining order and log out. That tells the
complete story of the engine in one take.

**Success metrics**:
- `./build/ome_main` starts and prints: `FIX 4.2 gateway listening on :9000`
- `python3 demo/fix_client.py` runs without error, prints at least 2 ExecutionReports
  showing matched trades
- All previous 195 tests still pass
- 20+ new TCP/gateway tests pass, TSan clean (matching thread + gateway thread)
- `demo/demo.gif` is committed and visible in the README on GitHub
- You can demo the running system live in under 2 minutes

---

## After Day 28: Interview narrative

With these three days done, the project story becomes:

> "I built a complete exchange from scratch in C++17 — FIX 4.2 protocol,
> multi-symbol routing, lock-free producer-consumer pipeline, zero-allocation hot path,
> and a TCP server that you can connect to right now and submit real orders.
> 195+ tests, sanitizer-clean on ASan, TSan, and UBSan."

That is a significantly stronger narrative than a library with a demo `main.cpp`.
