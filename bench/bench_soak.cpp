// bench/bench_soak.cpp
//
// Duration-based soak test: runs a realistic mixed workload for a fixed
// wall-clock duration, reports throughput every 30 seconds, and checks
// the book-uncrossed invariant throughout.
//
// Usage:
//   ./ome_bench_soak            # 15 minutes
//   ./ome_bench_soak 60         # 1-minute smoke test
//   ./ome_bench_soak 3600       # 1-hour endurance
//
// Exit code: 0 = PASSED, 1 = invariant violation detected.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "../include/order_book.hpp"
#include "../include/order.hpp"

using Clock = std::chrono::steady_clock;
using Sec   = std::chrono::seconds;
using Frac  = std::chrono::duration<double>;

// ── Configuration ─────────────────────────────────────────────────────────────

static constexpr int    DEFAULT_DURATION_S = 900;  // 15 minutes
static constexpr int    REPORT_INTERVAL_S  = 30;
static constexpr int    CHECK_EVERY        = 1000; // invariant check every N ops
static constexpr int    BATCH              = 500;  // ops between clock reads
static constexpr size_t MAX_LIVE           = 20000;// cap on tracked live orders

static constexpr Price MID  = 100 * PRICE_SCALE;  // $100.00
static constexpr Price TICK = PRICE_SCALE / 100;  // $0.01

// ── Workload mix ──────────────────────────────────────────────────────────────
// 50% new limit, 20% cancel, 15% IOC, 10% amend, 5% FOK

// ── Soak harness ──────────────────────────────────────────────────────────────

struct Soak {
    OrderBook            book;
    std::mt19937         rng{42};
    std::vector<OrderId> live;
    OrderId              next_id = 1;

    // stats
    uint64_t ops        = 0;
    uint64_t trades     = 0;
    uint64_t volume     = 0;
    uint64_t violations = 0;

    std::uniform_int_distribution<int> tick_d{-50, 50};
    std::uniform_int_distribution<int> qty_d {10, 500};
    std::uniform_int_distribution<int> pct_d {0, 99};

    Soak() {
        live.reserve(MAX_LIVE);
        book.set_trade_callback([this](const TradeEvent& te) {
            trades++;
            volume += te.quantity;
        });
    }

    Price rand_price() {
        return std::max(MID + static_cast<Price>(tick_d(rng)) * TICK, TICK);
    }

    Quantity rand_qty() {
        return static_cast<Quantity>(qty_d(rng));
    }

    // Pick an index into live[], swap-erase and return the ID.
    OrderId pop_live() {
        std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
        size_t idx    = pick(rng);
        OrderId id    = live[idx];
        live[idx]     = live.back();
        live.pop_back();
        return id;
    }

    void step() {
        const int r = pct_d(rng);

        // When the tracked live list is full, redirect new-limit ops to cancels
        // to prevent the book from growing past the pool capacity.
        if (r < 50 && live.size() >= MAX_LIVE) {
            if (!live.empty()) book.cancel_order(pop_live());
            ops++;
            return;
        }

        if (r < 50 || live.empty()) {
            // New limit order — 20 rotating traders so trades actually happen
            Side     s      = (rng() & 1) ? Side::Buy : Side::Sell;
            uint32_t trader = static_cast<uint32_t>((next_id % 20) + 1);
            Order*   rest   = book.create_order(next_id, 1, trader,
                                                s, rand_price(), rand_qty(),
                                                0, OrderType::Limit);
            // Only track if it rested in the book and there's room
            if (rest && live.size() < MAX_LIVE)
                live.push_back(next_id);
            next_id++;

        } else if (r < 70) {
            // Cancel
            book.cancel_order(pop_live());

        } else if (r < 85) {
            // IOC — uses trader 21 so it can match against all resting orders
            Side s = (rng() & 1) ? Side::Buy : Side::Sell;
            book.create_order(next_id++, 1, 21, s, rand_price(), rand_qty(),
                              0, OrderType::IOC);

        } else if (r < 95) {
            // Amend qty + price of a random live order
            std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
            book.amend_order(live[pick(rng)], rand_qty(), rand_price());

        } else {
            // FOK — uses trader 22
            Side s = (rng() & 1) ? Side::Buy : Side::Sell;
            book.create_order(next_id++, 1, 22, s, rand_price(), rand_qty(),
                              0, OrderType::FOK);
        }

        ops++;
        if (ops % CHECK_EVERY == 0)
            check_invariants();
    }

    void check_invariants() {
        auto bid = book.get_best_bid();
        auto ask = book.get_best_ask();
        if (bid.valid && ask.valid && bid.price >= ask.price) {
            printf("[VIOLATION] crossed book at op %llu: bid=%s >= ask=%s\n",
                   (unsigned long long)ops,
                   price_to_string(bid.price).c_str(),
                   price_to_string(ask.price).c_str());
            violations++;
        }
    }
};

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    const int duration_s = (argc >= 2) ? std::atoi(argv[1]) : DEFAULT_DURATION_S;

    printf("=== OME Soak Test ===\n");
    printf("Duration      : %d s\n", duration_s);
    printf("Report every  : %d s\n", REPORT_INTERVAL_S);
    printf("Workload mix  : 50%% limit  20%% cancel  15%% IOC  10%% amend  5%% FOK\n");
    printf("Invariant check every %d ops\n\n", CHECK_EVERY);

    printf("%-9s  %-12s  %-10s  %-16s  %-10s  %-8s  %-6s\n",
           "elapsed", "total_ops", "trades", "volume", "ops/s", "live", "pool_hwm");
    printf("%-9s  %-12s  %-10s  %-16s  %-10s  %-8s  %-6s\n",
           "-------", "---------", "------", "------", "-----", "----", "--------");

    Soak soak;

    const auto t_start      = Clock::now();
    const auto t_end        = t_start + Sec(duration_s);
    auto       t_next_print = t_start + Sec(REPORT_INTERVAL_S);
    auto       t_snapshot   = t_start;
    uint64_t   ops_snapshot = 0;

    while (true) {
        for (int i = 0; i < BATCH; ++i)
            soak.step();

        const auto now = Clock::now();
        if (now >= t_end) break;

        if (now >= t_next_print) {
            const long long elapsed_s = std::chrono::duration_cast<Sec>(now - t_start).count();
            const double    delta_s   = std::chrono::duration_cast<Frac>(now - t_snapshot).count();
            const double    ops_per_s = (delta_s > 0.0)
                                        ? static_cast<double>(soak.ops - ops_snapshot) / delta_s
                                        : 0.0;
            const auto pool = soak.book.get_pool_stats();

            printf("[T+%4llds]  %-12llu  %-10llu  %-16llu  %-10.0f  %-8zu  %-6zu\n",
                   elapsed_s,
                   (unsigned long long)soak.ops,
                   (unsigned long long)soak.trades,
                   (unsigned long long)soak.volume,
                   ops_per_s,
                   soak.live.size(),
                   pool.high_water_mark);

            ops_snapshot   = soak.ops;
            t_snapshot     = now;
            t_next_print  += Sec(REPORT_INTERVAL_S);
        }
    }

    // ── Final report ──────────────────────────────────────────────────────────

    const long long total_s   = std::chrono::duration_cast<Sec>(Clock::now() - t_start).count();
    const double    avg_ops_s = (total_s > 0) ? static_cast<double>(soak.ops) / total_s : 0.0;
    const auto      pool      = soak.book.get_pool_stats();

    printf("\n=== Final Report ===\n");
    printf("Actual duration     : %lld s\n",    total_s);
    printf("Total ops           : %llu\n",       (unsigned long long)soak.ops);
    printf("Total trades        : %llu\n",       (unsigned long long)soak.trades);
    printf("Total volume        : %llu\n",       (unsigned long long)soak.volume);
    printf("Avg throughput      : %.0f ops/s\n", avg_ops_s);
    printf("Invariant violations: %llu\n",       (unsigned long long)soak.violations);
    printf("Pool capacity       : %zu\n",        pool.capacity);
    printf("Pool high-water     : %zu\n",        pool.high_water_mark);
    printf("Pool heap fallbacks : %zu\n",        pool.heap_fallbacks);
    printf("Live orders at exit : %zu\n",        soak.live.size());
    printf("\n%s\n", soak.violations == 0 ? "PASSED" : "FAILED");

    return soak.violations == 0 ? 0 : 1;
}
