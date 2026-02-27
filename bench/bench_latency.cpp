#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>
#include <cstdio>
#include "../include/order_book.hpp"
#include "../include/order.hpp"

static void print_stats(const char* label, const std::vector<int64_t>& sorted_lat)
{
    const size_t  N     = sorted_lat.size();
    int64_t       total = std::accumulate(sorted_lat.begin(), sorted_lat.end(),
                                          int64_t{0});
    int64_t       mean  = total / static_cast<int64_t>(N);

    auto pct = [&](double p) -> int64_t {
        auto idx = static_cast<size_t>(p / 100.0 * static_cast<double>(N));
        if (idx >= N) idx = N - 1;
        return sorted_lat[idx];
    };

    printf("%s (N=%zu):\n",           label,                      N);
    printf("    min:   %6lld ns\n",   (long long)sorted_lat.front());
    printf("    p50:   %6lld ns\n",   (long long)pct(50.0));
    printf("    p95:   %6lld ns\n",   (long long)pct(95.0));
    printf("    p99:   %6lld ns\n",   (long long)pct(99.0));
    printf("    p999:  %6lld ns\n",   (long long)pct(99.9));
    printf("    max:   %6lld ns\n",   (long long)sorted_lat.back());
    printf("    mean:  %6lld ns\n\n", (long long)mean);
}

// Fill book with n resting orders spread across ±100 ticks around mid.
static void populate_book(OrderBook& book, int n, Price mid, Price tick,
                          std::mt19937& rng, uint64_t id_start)
{
    std::uniform_int_distribution<int>     qty_dist(100, 500);
    std::uniform_real_distribution<double> dice(0.0, 1.0);
    std::uniform_int_distribution<int>     tick_dist(-100, 100);

    for (int i = 0; i < n; ++i) {
        Side  s = (dice(rng) < 0.5) ? Side::Buy : Side::Sell;
        Price p = mid + static_cast<Price>(tick_dist(rng)) * tick;
        if (p <= 0) p = tick;
        book.create_order(id_start + static_cast<uint64_t>(i), 1, 1, s, p,
                          static_cast<Quantity>(qty_dist(rng)), 0,
                          OrderType::Limit);
    }
}

int main()
{
    const Price MID        = to_price(100.0);
    const Price TICK       = to_price(0.01);
    const int   BOOK_DEPTH = 2000;
    const int   WARMUP     = 5'000;
    const int   MEASURED   = 50'000;

    std::mt19937 rng(42);

    printf("=== Benchmark 2: Latency Under Load ===\n");
    printf("Book depth: %d resting orders | Measured ops: %d\n\n",
           BOOK_DEPTH, MEASURED);

    // ── Warmup ──────────────────────────────────────────────────────────────
    {
        OrderBook book;
        for (int i = 0; i < WARMUP; ++i)
            book.create_order(static_cast<uint64_t>(i + 1), 1, 1,
                              Side::Buy, MID - TICK, 100, 0, OrderType::Limit);
    }

    // ── 2a: Limit add under load (non-crossing, measures pure insertion) ────
    {
        OrderBook book;
        populate_book(book, BOOK_DEPTH, MID, TICK, rng, 1);

        // Prices that don't cross: buys well below mid, sells well above mid
        const Price BUY_PRICE  = MID - static_cast<Price>(200) * TICK;
        const Price SELL_PRICE = MID + static_cast<Price>(200) * TICK;
        const uint64_t base    = 1'000'000;

        std::vector<int64_t> lat(MEASURED);
        for (int i = 0; i < MEASURED; ++i) {
            Side  s = (i % 2 == 0) ? Side::Buy : Side::Sell;
            Price p = (s == Side::Buy) ? BUY_PRICE : SELL_PRICE;

            auto t0 = std::chrono::high_resolution_clock::now();
            book.create_order(base + static_cast<uint64_t>(i), 1, 1,
                              s, p, 100, 0, OrderType::Limit);
            auto t1 = std::chrono::high_resolution_clock::now();

            lat[i] = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         t1 - t0).count();
        }
        std::sort(lat.begin(), lat.end());
        print_stats("2a: Add limit order (no match, book depth ~2000)", lat);
    }

    // ── 2b: IOC match under load (measures match path with depth) ────────────
    {
        OrderBook book;
        // Large per-order qty so orders survive 50K single-unit IOC hits
        populate_book(book, BOOK_DEPTH, MID, TICK, rng, 2'000'000);

        const uint64_t base = 3'000'000;

        std::vector<int64_t> lat(MEASURED);
        for (int i = 0; i < MEASURED; ++i) {
            // Aggressive IOC at mid price, qty=1 — matches one unit at best price
            Side s = (i % 2 == 0) ? Side::Buy : Side::Sell;

            auto t0 = std::chrono::high_resolution_clock::now();
            book.create_order(base + static_cast<uint64_t>(i), 1, 1,
                              s, MID, 1, 0, OrderType::IOC);
            auto t1 = std::chrono::high_resolution_clock::now();

            lat[i] = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         t1 - t0).count();
        }
        std::sort(lat.begin(), lat.end());
        print_stats("2b: IOC match (qty=1, book depth ~2000)", lat);
    }

    return 0;
}
