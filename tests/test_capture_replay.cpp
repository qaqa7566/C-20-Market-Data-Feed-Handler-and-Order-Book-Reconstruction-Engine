#include <gtest/gtest.h>

#include <string>

#include "mdfh/book_builder.hpp"
#include "mdfh/capture.hpp"
#include "mdfh/replay.hpp"
#include "test_util.hpp"

using namespace mdfh;

namespace {
std::string tmp_path(const char* name) {
    return ::testing::TempDir() + std::string(name);
}

std::uint64_t replay_fingerprint(std::span<const std::byte> feed, const ReplayConfig& cfg) {
    BookBuilder builder;
    ReplayDriver driver(builder);
    driver.replay(feed, cfg);
    return book_fingerprint(builder.manager());
}
}  // namespace

TEST(Capture, WriteThenReadRoundTrips) {
    const std::string path = tmp_path("mdfh_rt.cap");
    auto feed = test::build_sim_feed(5'000, 3, 9);

    // Write the raw feed bytes through the capture writer by re-simulating.
    {
        CaptureWriter w;
        ASSERT_TRUE(w.open(path));
        SimConfig cfg; cfg.num_messages = 5'000; cfg.num_symbols = 3; cfg.seed = 9;
        cfg.heartbeat_interval = 0;
        MarketSimulator sim(cfg);
        auto n = sim.run([&](Sequence s, TimestampNs t, const auto& b) { w.write(s, t, b); });
        w.close();
        EXPECT_EQ(n, 5'000u);
    }
    CaptureReader r;
    ASSERT_TRUE(r.open(path));
    EXPECT_EQ(r.message_count(), 5'000u);
    EXPECT_EQ(r.messages().size(), feed.size());
}

TEST(Replay, DeterministicAcrossRunsAndSpeeds) {
    auto feed = test::build_sim_feed(3'000, 4, 55);

    std::uint64_t max1 = replay_fingerprint(feed, {ReplayMode::MaxSpeed, 1.0});
    std::uint64_t max2 = replay_fingerprint(feed, {ReplayMode::MaxSpeed, 1.0});
    EXPECT_EQ(max1, max2);  // identical input -> identical final state

    // A timed (accelerated) replay must converge to the same book state.
    std::uint64_t accel = replay_fingerprint(feed, {ReplayMode::Accelerated, 2000.0});
    EXPECT_EQ(max1, accel);
}

TEST(Capture, RejectsMissingAndBadFiles) {
    CaptureReader r;
    EXPECT_FALSE(r.open(tmp_path("does_not_exist.cap")));

    const std::string bad = tmp_path("mdfh_bad.cap");
    std::FILE* f = std::fopen(bad.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    const char junk[] = "NOTACAPTURE1234567890abcd";
    std::fwrite(junk, 1, sizeof(junk), f);
    std::fclose(f);

    CaptureReader r2;
    EXPECT_FALSE(r2.open(bad));
    EXPECT_FALSE(r2.valid());
}
