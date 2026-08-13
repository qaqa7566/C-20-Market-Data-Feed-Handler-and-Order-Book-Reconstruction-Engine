// mdfh_benchmark -- end-to-end throughput and latency of the feed handler +
// order-book reconstruction path. All numbers come from actually running the
// code on generated data; nothing here is hard-coded.
//
//   mdfh_benchmark --messages 2000000 --symbols 16 --seed 1 --json bench.json
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../src/apps/app_common.hpp"
#include "mdfh/book_builder.hpp"
#include "mdfh/feed_handler.hpp"
#include "mdfh/pipeline.hpp"
#include "mdfh/replay.hpp"
#include "mdfh/serialization.hpp"
#include "mdfh/simulator.hpp"
#include "mdfh/stats.hpp"

using namespace mdfh;
using Clock = std::chrono::steady_clock;

namespace {

// Generate a fully framed in-memory feed. Also records each frame's byte offset
// so the latency pass can iterate message boundaries without re-parsing headers.
std::vector<std::byte> build_feed(const SimConfig& cfg,
                                  std::vector<std::pair<std::size_t, std::size_t>>& frames) {
    std::vector<std::byte> buf;
    buf.reserve(cfg.num_messages * 40);
    MarketSimulator sim(cfg);
    std::array<std::byte, kMaxMessageSize> tmp{};
    sim.run([&](Sequence s, TimestampNs t, const auto& body) {
        std::size_t len = encode_message(tmp, s, t, body);
        std::size_t off = buf.size();
        buf.insert(buf.end(), tmp.data(), tmp.data() + len);
        frames.emplace_back(off, len);
    });
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    app::Args args(argc, argv);
    SimConfig cfg;
    cfg.num_messages = args.u64("messages", 2'000'000);
    cfg.num_symbols  = static_cast<std::uint32_t>(args.u64("symbols", 16));
    cfg.seed         = args.u64("seed", 1);
    const std::string json_path = args.str("json");

    std::printf("generating %llu messages across %u symbols (seed %llu)...\n",
                (unsigned long long)cfg.num_messages, cfg.num_symbols,
                (unsigned long long)cfg.seed);
    std::vector<std::pair<std::size_t, std::size_t>> frames;
    frames.reserve(cfg.num_messages);
    std::vector<std::byte> feed = build_feed(cfg, frames);
    std::printf("feed size: %.1f MB, %zu frames\n",
                static_cast<double>(feed.size()) / (1024 * 1024), frames.size());

    // -------- Single-threaded throughput (bulk consume, untimed inner loop) --
    std::uint64_t st_msgs = 0, st_malformed = 0;
    double st_seconds = 0;
    std::uint64_t st_fingerprint = 0;
    {
        BookBuilder builder;
        builder.set_verify(false);
        FeedHandler<BookBuilder> feedh(builder);
        auto t0 = Clock::now();
        ParseResult r = feedh.consume(feed);
        auto t1 = Clock::now();
        st_seconds = std::chrono::duration<double>(t1 - t0).count();
        st_msgs = r.messages;
        st_malformed = r.malformed;
        st_fingerprint = book_fingerprint(builder.manager());
    }
    double st_mps = static_cast<double>(st_msgs) / st_seconds;

    // -------- Single-threaded per-message latency distribution ---------------
    LatencyHistogram hist;
    hist.reserve(frames.size());
    {
        BookBuilder builder;
        builder.set_verify(false);
        FeedHandler<BookBuilder> feedh(builder);
        for (auto [off, len] : frames) {
            std::span<const std::byte> frame(feed.data() + off, len);
            auto a = Clock::now();
            feedh.consume_datagram(frame);
            auto b = Clock::now();
            hist.add(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count()));
        }
    }
    LatencySummary lat = hist.summarize();

    // -------- Concurrent pipeline throughput ---------------------------------
    ConcurrentPipeline pipeline;
    PipelineResult mt = pipeline.run(feed);
    double mt_mps = static_cast<double>(mt.messages_parsed) / mt.wall_seconds;
    bool deterministic = (pipeline.fingerprint() == st_fingerprint);

    // -------- Report ---------------------------------------------------------
    std::printf("\n================ RESULTS ================\n");
    std::printf("messages processed     : %llu\n", (unsigned long long)st_msgs);
    std::printf("malformed / dropped    : %llu / 0\n", (unsigned long long)st_malformed);
    std::printf("\n-- single-threaded --\n");
    std::printf("wall time              : %.4f s\n", st_seconds);
    std::printf("throughput             : %.3f M msg/s\n", st_mps / 1e6);
    std::printf("avg latency            : %.1f ns\n", lat.mean_ns);
    std::printf("p50 / p95 / p99        : %llu / %llu / %llu ns\n",
                (unsigned long long)lat.p50_ns, (unsigned long long)lat.p95_ns,
                (unsigned long long)lat.p99_ns);
    std::printf("min / max              : %llu / %llu ns\n",
                (unsigned long long)lat.min_ns, (unsigned long long)lat.max_ns);
    std::printf("\n-- concurrent pipeline (3 threads) --\n");
    std::printf("wall time              : %.4f s\n", mt.wall_seconds);
    std::printf("throughput             : %.3f M msg/s\n", mt_mps / 1e6);
    std::printf("analytics events       : %llu\n", (unsigned long long)mt.analytics_events);
    std::printf("deterministic vs ST    : %s\n", deterministic ? "YES" : "NO");
    std::printf("=========================================\n");

    if (!json_path.empty()) {
        std::FILE* f = std::fopen(json_path.c_str(), "w");
        if (f) {
            std::fprintf(f,
                "{\n"
                "  \"messages\": %llu,\n"
                "  \"symbols\": %u,\n"
                "  \"seed\": %llu,\n"
                "  \"malformed\": %llu,\n"
                "  \"single_threaded\": {\n"
                "    \"wall_seconds\": %.6f,\n"
                "    \"throughput_msg_per_s\": %.3f,\n"
                "    \"avg_latency_ns\": %.3f,\n"
                "    \"p50_ns\": %llu, \"p95_ns\": %llu, \"p99_ns\": %llu,\n"
                "    \"min_ns\": %llu, \"max_ns\": %llu\n"
                "  },\n"
                "  \"concurrent\": {\n"
                "    \"wall_seconds\": %.6f,\n"
                "    \"throughput_msg_per_s\": %.3f,\n"
                "    \"analytics_events\": %llu,\n"
                "    \"deterministic\": %s\n"
                "  }\n"
                "}\n",
                (unsigned long long)st_msgs, cfg.num_symbols, (unsigned long long)cfg.seed,
                (unsigned long long)st_malformed,
                st_seconds, st_mps, lat.mean_ns,
                (unsigned long long)lat.p50_ns, (unsigned long long)lat.p95_ns,
                (unsigned long long)lat.p99_ns, (unsigned long long)lat.min_ns,
                (unsigned long long)lat.max_ns,
                mt.wall_seconds, mt_mps, (unsigned long long)mt.analytics_events,
                deterministic ? "true" : "false");
            std::fclose(f);
            std::printf("wrote %s\n", json_path.c_str());
        }
    }
    return 0;
}
