#include <benchmark/benchmark.h>
#include <vector>
#include "../include/order_book.hpp"
#include "../include/order.hpp"

static void BM_AddLimitOrdersNoMatch(benchmark::State &state)
{
    const int N = static_cast<int>(state.range(0));

    std::vector<Order> orders;
    orders.reserve(N);
    for (int i = 0; i < N; i++)
        orders.emplace_back(i + 1, 1, 1, Side::Buy, 10000 + i, 100, 0, OrderType::Limit);

    for (auto _ : state)
    {
        for (int i = 0; i < N; i++)
            orders[i].quantity = 100;
        
        OrderBook book;
        for (int i = 0; i < N; i++)
            book.add_order(&orders[i]);
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_AddLimitOrdersNoMatch)
    ->Arg(1000)
    ->Arg(10000)
    ->Unit(benchmark::kMicrosecond);


static void BM_ThroughputMatchingOrders(benchmark::State &state)
{
    const int N = static_cast<int>(state.range(0));

    std::vector<Order> buys(N), sells(N);
    for (int i = 0; i < N; i++)
    {
        buys[i] = Order(i + 1, 1, 1, Side::Buy, 10000, 100, 0, OrderType::Limit);
        sells[i] = Order(N + i + 1, 1, 2, Side::Sell, 10000, 100, 0, OrderType::Limit);
    }

    for (auto _ : state)
    {
        for (int i = 0; i < N; i++)
        {
            buys[i].quantity = 100;
            sells[i].quantity = 100;
        }

        OrderBook book;
        for (int i = 0; i < N; i++)
        {
            book.match(&buys[i]);
            book.match(&sells[i]);
        }
    }
    state.SetItemsProcessed(state.iterations() * N * 2);
}
BENCHMARK(BM_ThroughputMatchingOrders)
    ->Arg(1000)
    ->Arg(10000)
    ->Unit(benchmark::kMicrosecond);