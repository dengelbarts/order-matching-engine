#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include "../include/spsc_queue.hpp"

TEST(SpscQueueTest, EmptyOnConstruction)
{
    SpscQueue<int, 8> q;
    EXPECT_TRUE(q.empty());
    int val;
    EXPECT_FALSE(q.try_pop(val));
}

TEST(SpscQueueTest, SinglePushPop)
{
    SpscQueue<int, 8> q;
    EXPECT_TRUE(q.try_push(42));
    EXPECT_FALSE(q.empty());
    int val = 0;
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(q.empty());
}

TEST(SpscQueueTest, FifoOrdering)
{
    SpscQueue<int, 16> q;
    for (int i = 0; i < 10; ++i)
        EXPECT_TRUE(q.try_push(i));
    for (int i = 0; i < 10; ++i)
    {
        int val;
        EXPECT_TRUE(q.try_pop(val));
        EXPECT_EQ(val, i);
    }
}

TEST(SpscQueueTest, CapacityLimit)
{
    SpscQueue<int, 8> q;
    for (std::size_t i = 0; i < q.capacity(); ++i)
        EXPECT_TRUE(q.try_push(static_cast<int>(i)));
    EXPECT_FALSE(q.try_push(999));
}

TEST(SpscQueueTest, WrapAround)
{
    SpscQueue<int, 4> q;
    EXPECT_TRUE(q.try_push(1));
    EXPECT_TRUE(q.try_push(2));
    int v;
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, 1);
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, 2);
    EXPECT_TRUE(q.try_push(3));
    EXPECT_TRUE(q.try_push(4));
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, 3);
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, 4);
    EXPECT_TRUE(q.empty());
}

TEST(SpscQueueTest, PopFromEmptyReturnsFalse)
{
    SpscQueue<int, 8> q;
    int val;
    EXPECT_FALSE(q.try_pop(val));
    q.try_push(1);
    int x;
    q.try_pop(x);
    EXPECT_FALSE(q.try_pop(val));
}

TEST(SpscQueueTest, ConcurrentOneMillion)
{
    constexpr int N = 1'000'000;
    SpscQueue<int, 1024> q;

    long long checksum_prod = 0;
    std::atomic<long long> checksum_cons{0};
    std::atomic<int> consumed{0};

    std::thread producer([&]
    {
        for (int i = 1; i <= N; ++i)
        {
            while(!q.try_push(i))
                ;
            checksum_prod += i;
        }
    });

    std::thread consumer([&]
    {
        int val;
        int count = 0;
        long long sum = 0;
        while (count < N)
        {
            if (q.try_pop(val))
            {
                sum += val;
                ++count;
            }
        }
        checksum_cons.store(sum, std::memory_order_relaxed);
        consumed.store(count, std::memory_order_relaxed);
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(consumed.load(), N);
    EXPECT_EQ(checksum_prod, checksum_cons.load());
    EXPECT_TRUE(q.empty());
}

TEST(SpscQueueTest, NoItemsLost)
{
    constexpr int N = 100'000;
    SpscQueue<int, 512> q;
    std::vector<bool> received(N + 1, false);

    std::thread producer([&]
    {
        for (int i = 1; i <= N; ++i)
            while (!q.try_push(i))
                ;
    });

    std::thread consumer([&]
    {
        int val, count = 0;
        while (count < N)
        {
            if (q.try_pop(val))
            {
            received[static_cast<std::size_t>(val)] = true;
            ++count;
            }
        }
    });

    producer.join();
    consumer.join();

    for (int i = 1; i <= N; ++i)
        EXPECT_TRUE(received[static_cast<std::size_t>(i)]) << "Item " << i << " was lost";
}