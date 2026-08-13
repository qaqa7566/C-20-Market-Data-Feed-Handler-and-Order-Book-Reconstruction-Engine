#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "mdfh/spsc_queue.hpp"

using namespace mdfh;

TEST(SPSCQueue, SingleThreadFifoAndCapacity) {
    SPSCQueue<int> q(4);  // rounds to 4 -> usable 3
    EXPECT_TRUE(q.empty());
    EXPECT_TRUE(q.try_push(1));
    EXPECT_TRUE(q.try_push(2));
    EXPECT_TRUE(q.try_push(3));
    EXPECT_FALSE(q.try_push(4));  // full
    int v = 0;
    EXPECT_TRUE(q.try_pop(v)); EXPECT_EQ(v, 1);
    EXPECT_TRUE(q.try_pop(v)); EXPECT_EQ(v, 2);
    EXPECT_TRUE(q.try_pop(v)); EXPECT_EQ(v, 3);
    EXPECT_FALSE(q.try_pop(v));  // empty
}

TEST(SPSCQueue, ConcurrentTransferPreservesOrder) {
    constexpr int N = 1'000'000;
    SPSCQueue<int> q(1024);

    std::thread producer([&] {
        for (int i = 0; i < N; ++i)
            while (!q.try_push(i)) std::this_thread::yield();
    });

    int expected = 0;
    std::thread consumer([&] {
        int v = 0;
        while (expected < N) {
            if (q.try_pop(v)) {
                ASSERT_EQ(v, expected);  // strict FIFO, no loss, no dup
                ++expected;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
    EXPECT_EQ(expected, N);
}
