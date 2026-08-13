#include <gtest/gtest.h>

#include "mdfh/book_builder.hpp"
#include "mdfh/feed_handler.hpp"
#include "test_util.hpp"

using namespace mdfh;

TEST(Simulator, SameSeedProducesIdenticalStream) {
    auto a = test::build_sim_feed(20'000, 4, 123);
    auto b = test::build_sim_feed(20'000, 4, 123);
    EXPECT_EQ(a, b);
}

TEST(Simulator, DifferentSeedProducesDifferentStream) {
    auto a = test::build_sim_feed(20'000, 4, 1);
    auto b = test::build_sim_feed(20'000, 4, 2);
    EXPECT_NE(a, b);
}

TEST(Simulator, ProducesConsistentUncrossedBooks) {
    auto feed = test::build_sim_feed(100'000, 6, 77);
    BookBuilder builder;
    builder.set_verify(false);
    FeedHandler<BookBuilder> fh(builder);
    fh.consume(feed);

    EXPECT_EQ(builder.manager().symbol_count(), 6u);
    EXPECT_TRUE(builder.manager().check_all_invariants());
    // Every cancel/modify/trade references a live order the simulator created,
    // so nothing should be rejected.
    EXPECT_EQ(builder.stats().rejected, 0u);
    EXPECT_EQ(fh.malformed(), 0u);
    EXPECT_GT(builder.stats().adds, 0u);
    EXPECT_GT(builder.stats().trades, 0u);
}
