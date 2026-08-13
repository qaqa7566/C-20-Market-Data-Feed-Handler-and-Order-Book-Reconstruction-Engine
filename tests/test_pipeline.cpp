#include <gtest/gtest.h>

#include "mdfh/book_builder.hpp"
#include "mdfh/pipeline.hpp"
#include "mdfh/replay.hpp"
#include "test_util.hpp"

using namespace mdfh;

// The concurrent pipeline must reconstruct the identical final book state as
// the single-threaded path -- this is the core concurrency-correctness check.
TEST(Pipeline, MatchesSingleThreadedFinalState) {
    auto feed = test::build_sim_feed(200'000, 8, 2024);

    // Single-threaded reference.
    BookBuilder ref;
    ref.set_verify(false);
    ReplayDriver driver(ref);
    driver.replay(feed, {ReplayMode::MaxSpeed, 1.0});
    std::uint64_t ref_fp = book_fingerprint(ref.manager());

    // Concurrent pipeline.
    ConcurrentPipeline pipeline;
    PipelineResult r = pipeline.run(feed);

    EXPECT_EQ(pipeline.fingerprint(), ref_fp);
    EXPECT_EQ(pipeline.final_symbol_count(), ref.manager().symbol_count());
    EXPECT_GT(r.messages_parsed, 0u);
    EXPECT_EQ(r.malformed, 0u);
}
