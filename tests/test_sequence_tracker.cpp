#include <gtest/gtest.h>

#include "mdfh/sequence_tracker.hpp"

using namespace mdfh;

TEST(SequenceTracker, InOrderStream) {
    SequenceTracker t;
    for (Sequence s = 100; s < 110; ++s) EXPECT_EQ(t.observe(s), SeqStatus::InOrder);
    EXPECT_EQ(t.stats().in_order, 10u);
    EXPECT_EQ(t.stats().duplicates, 0u);
    EXPECT_EQ(t.stats().missing, 0u);
    EXPECT_EQ(t.next_expected(), 110u);
}

TEST(SequenceTracker, DetectsDuplicate) {
    SequenceTracker t;
    EXPECT_EQ(t.observe(1), SeqStatus::InOrder);
    EXPECT_EQ(t.observe(2), SeqStatus::InOrder);
    EXPECT_EQ(t.observe(2), SeqStatus::Duplicate);
    EXPECT_EQ(t.stats().duplicates, 1u);
}

TEST(SequenceTracker, DetectsGapAndCountsMissing) {
    SequenceTracker t;
    EXPECT_EQ(t.observe(1), SeqStatus::InOrder);
    EXPECT_EQ(t.observe(5), SeqStatus::Gap);  // skipped 2,3,4
    EXPECT_EQ(t.stats().gap_events, 1u);
    EXPECT_EQ(t.stats().missing, 3u);
    EXPECT_EQ(t.next_expected(), 6u);
}

TEST(SequenceTracker, DetectsOutOfOrder) {
    SequenceTracker t;
    t.observe(1);
    t.observe(2);
    t.observe(4);                                   // gap
    EXPECT_EQ(t.observe(3), SeqStatus::OutOfOrder);  // late arrival behind HWM
    EXPECT_EQ(t.stats().out_of_order, 1u);
}

TEST(SequenceTracker, FirstMessageInitializesRegardlessOfValue) {
    SequenceTracker t;
    EXPECT_FALSE(t.initialized());
    EXPECT_EQ(t.observe(999'999), SeqStatus::InOrder);
    EXPECT_TRUE(t.initialized());
    EXPECT_EQ(t.next_expected(), 1'000'000u);
}
