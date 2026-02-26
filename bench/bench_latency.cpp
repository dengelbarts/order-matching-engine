#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cstdio>
#include "../include/order_book.hpp"
#include "../include/order.hpp"

int main()
{
    const int WARMUP = 10'000;
    const int MEASURED = 100'000;

    for (int i = 0; i < WARMUP; i++)
    {
        Order b(i + 1, 1, 1, Side::Buy, 10000, 100, 0, OrderType::Limit);
        Order s(WARMUP + i + 1, 1, 2, Side::Sell, 10000, 100, 0, OrderType::Limit);
        OrderBook book;
        book.match(&b);
        book.match(&s); 
    }

    std::vector<Order> buys(MEASURED), sells(MEASURED);
    for (int i = 0; i < MEASURED; i++)
    {
        buys[i] = Order(i + 1, 1, 1, Side::Buy, 10000, 100, 0, OrderType::Limit);
        sells[i] = Order(MEASURED + i + 1, 1, 2, Side::Sell, 10000, 100, 0, OrderType::Limit);
    }

    OrderBook book;
    for (int i = 0; i < MEASURED; i++)
        book.add_order(&buys[i]);

    std::vector<int64_t> latencies(MEASURED);

    for (int i = 0; i < MEASURED; i++)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        book.match(&sells[i]);
        auto t1 = std::chrono::high_resolution_clock::now();

        latencies[i] = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    }

    std::sort(latencies.begin(), latencies.end());

    auto percentile = [&](double p) -> int64_t
    {
        auto idx = static_cast<size_t>(p / 100.0 * static_cast<double>(MEASURED));
        if (idx >= static_cast<size_t>(MEASURED)) idx = MEASURED - 1;
        return latencies[idx];
    };

    int64_t total = std::accumulate(latencies.begin(), latencies.end(), int64_t{0});
    int64_t mean = total / MEASURED;

    printf("=== Latency Benchmark (N=%d sell-match operations) ===\n", MEASURED);
    printf("   Each measurement = 1 sell limit matched against 1 resting buy\n\n");
    printf("   min:   %6lld ns\n", (long long)latencies.front());
    printf("   p50:   %6lld ns\n", (long long)percentile(50.0));
    printf("   p95:   %6lld ns\n", (long long)percentile(95.0));
    printf("   p99:   %6lld ns\n", (long long)percentile(99.0));
    printf("   p999:  %6lld ns\n", (long long)percentile(99.9));
    printf("   max:   %6lld ns\n", (long long)latencies.back());
    printf("   mean:  %6lld ns\n", (long long)mean);

    return 0;
}