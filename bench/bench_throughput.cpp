#include "../include/order.hpp"
#include "../include/order_book.hpp"

#include <random>
#include <vector>

#include <benchmark/benchmark.h>

// ─── Realistic Workload Generator ─────────────────────────────────────────────
// Mix: 60% limit orders, 20% cancels, 10% IOC, 10% amend
// Prices distributed uniformly within ±50 ticks around a mid price.

struct WorkloadOp {
    enum class Type : uint8_t { AddLimit, Cancel, AddIOC, Amend };

    Type type = Type::AddLimit;
    uint64_t order_id = 0;
    Side side = Side::Buy;
    Price price = 0;
    Quantity quantity = 0;
    uint64_t target_id = 0; // for Cancel
    Price new_price = 0;    // for Amend
    Quantity new_qty = 0;   // for Amend
};

static std::vector<WorkloadOp>
make_realistic_workload(int N, uint64_t id_base = 1, unsigned seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dice(0.0, 1.0);
    std::uniform_int_distribution<int> qty_dist(10, 500);
    std::uniform_int_distribution<int> tick_dist(-50, 50);

    const Price MID = to_price(100.0);
    const Price TICK = to_price(0.01);

    std::vector<WorkloadOp> ops;
    ops.reserve(N);

    std::vector<uint64_t> live_ids;
    live_ids.reserve(4096);
    uint64_t next_id = id_base;

    for (int i = 0; i < N; ++i) {
        double r = dice(rng);
        WorkloadOp op{};

        if (r < 0.60 || live_ids.empty()) {
            op.type = WorkloadOp::Type::AddLimit;
            op.order_id = next_id++;
            op.side = (dice(rng) < 0.5) ? Side::Buy : Side::Sell;
            Price p = MID + static_cast<Price>(tick_dist(rng)) * TICK;
            op.price = (p > 0) ? p : TICK;
            op.quantity = static_cast<Quantity>(qty_dist(rng));
            live_ids.push_back(op.order_id);

        } else if (r < 0.80) {
            op.type = WorkloadOp::Type::Cancel;
            std::uniform_int_distribution<size_t> pick(0, live_ids.size() - 1);
            size_t idx = pick(rng);
            op.target_id = live_ids[idx];
            live_ids[idx] = live_ids.back();
            live_ids.pop_back();

        } else if (r < 0.90) {
            op.type = WorkloadOp::Type::AddIOC;
            op.order_id = next_id++;
            op.side = (dice(rng) < 0.5) ? Side::Buy : Side::Sell;
            Price p = MID + static_cast<Price>(tick_dist(rng)) * TICK;
            op.price = (p > 0) ? p : TICK;
            op.quantity = static_cast<Quantity>(qty_dist(rng));

        } else {
            op.type = WorkloadOp::Type::Amend;
            std::uniform_int_distribution<size_t> pick(0, live_ids.size() - 1);
            op.order_id = live_ids[pick(rng)];
            Price p = MID + static_cast<Price>(tick_dist(rng)) * TICK;
            op.new_price = (p > 0) ? p : TICK;
            op.new_qty = static_cast<Quantity>(qty_dist(rng));
        }

        ops.push_back(op);
    }
    return ops;
}

static void execute_workload(OrderBook& book, const std::vector<WorkloadOp>& ops) {
    for (const auto& op : ops) {
        switch (op.type) {
        case WorkloadOp::Type::AddLimit:
            book.create_order(
                op.order_id, 1, 1, op.side, op.price, op.quantity, 0, OrderType::Limit);
            break;
        case WorkloadOp::Type::Cancel:
            book.cancel_order(op.target_id);
            break;
        case WorkloadOp::Type::AddIOC:
            book.create_order(op.order_id, 1, 1, op.side, op.price, op.quantity, 0, OrderType::IOC);
            break;
        case WorkloadOp::Type::Amend:
            book.amend_order(op.order_id, op.new_price, op.new_qty);
            break;
        }
    }
}

// ─── Benchmark 1: Sustained Throughput ────────────────────────────────────────
// Replays a realistic mixed workload of N operations and reports items/second.
static void BM_SustainedThroughput(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    auto wl = make_realistic_workload(N);

    for (auto _ : state) {
        OrderBook book;
        execute_workload(book, wl);
        benchmark::DoNotOptimize(book);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(N));
}
BENCHMARK(BM_SustainedThroughput)->Arg(100'000)->Arg(1'000'000)->Unit(benchmark::kMillisecond);

// ─── Benchmark 3: Cancel-Heavy Workload ───────────────────────────────────────
// Simulates a volatile market: 60% adds, 35% cancels, 5% IOC.
static void BM_CancelHeavy(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));

    std::mt19937 rng(77);
    std::uniform_real_distribution<double> dice(0.0, 1.0);
    std::uniform_int_distribution<int> qty_dist(10, 200);
    std::uniform_int_distribution<int> tick_dist(-50, 50);

    const Price MID = to_price(100.0);
    const Price TICK = to_price(0.01);

    struct CancelOp {
        enum class T : uint8_t { Add, Cancel, IOC };
        T type;
        uint64_t id;
        Side side;
        Price price;
        Quantity qty;
    };

    std::vector<CancelOp> ops;
    std::vector<uint64_t> live;
    ops.reserve(N);
    live.reserve(4096);
    uint64_t next = 1;

    for (int i = 0; i < N; ++i) {
        double r = dice(rng);
        if (r < 0.60 || live.empty()) {
            Price p = MID + static_cast<Price>(tick_dist(rng)) * TICK;
            if (p <= 0)
                p = TICK;
            Side s = (dice(rng) < 0.5) ? Side::Buy : Side::Sell;
            ops.push_back({CancelOp::T::Add, next, s, p, static_cast<Quantity>(qty_dist(rng))});
            live.push_back(next++);
        } else if (r < 0.95) {
            std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
            size_t idx = pick(rng);
            ops.push_back({CancelOp::T::Cancel, live[idx], Side::Buy, 0, 0});
            live[idx] = live.back();
            live.pop_back();
        } else {
            Price p = MID + static_cast<Price>(tick_dist(rng)) * TICK;
            if (p <= 0)
                p = TICK;
            Side s = (dice(rng) < 0.5) ? Side::Buy : Side::Sell;
            ops.push_back({CancelOp::T::IOC, next++, s, p, static_cast<Quantity>(qty_dist(rng))});
        }
    }

    for (auto _ : state) {
        OrderBook book;
        for (const auto& op : ops) {
            switch (op.type) {
            case CancelOp::T::Add:
                book.create_order(op.id, 1, 1, op.side, op.price, op.qty, 0, OrderType::Limit);
                break;
            case CancelOp::T::Cancel:
                book.cancel_order(op.id);
                break;
            case CancelOp::T::IOC:
                book.create_order(op.id, 1, 1, op.side, op.price, op.qty, 0, OrderType::IOC);
                break;
            }
        }
        benchmark::DoNotOptimize(book);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(N));
}
BENCHMARK(BM_CancelHeavy)->Arg(100'000)->Unit(benchmark::kMillisecond);

// ─── Benchmark 4: Deep Book Stress ────────────────────────────────────────────
// Builds LEVELS unique price levels on the ask side, then sweeps with a market buy.
static void BM_DeepBook(benchmark::State& state) {
    const int LEVELS = static_cast<int>(state.range(0));
    const Price TICK = to_price(0.01);

    std::vector<uint64_t> ids(LEVELS);
    std::vector<Price> prices(LEVELS);
    for (int i = 0; i < LEVELS; ++i) {
        ids[i] = static_cast<uint64_t>(i + 1);
        prices[i] = static_cast<Price>(i + 1) * TICK * 100; // 1.00, 1.01, 1.02…
    }
    const uint64_t sweep_id = static_cast<uint64_t>(LEVELS + 1);

    for (auto _ : state) {
        OrderBook book;
        for (int i = 0; i < LEVELS; ++i)
            book.create_order(ids[i], 1, 1, Side::Sell, prices[i], 100, 0, OrderType::Limit);

        // Market buy sweeps the entire ask side
        book.create_order(sweep_id,
                          1,
                          2,
                          Side::Buy,
                          0,
                          static_cast<Quantity>(100) * static_cast<Quantity>(LEVELS),
                          0,
                          OrderType::Market);
        benchmark::DoNotOptimize(book);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(LEVELS));
}
BENCHMARK(BM_DeepBook)->Arg(1'000)->Arg(10'000)->Unit(benchmark::kMillisecond);
