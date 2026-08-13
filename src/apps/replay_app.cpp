// mdfh_replay -- replay a capture file through the reconstruction engine.
//
//   mdfh_replay --in data/feed.cap [--mode max|realtime|accel] [--speed 10]
#include <cstdio>
#include <string>

#include "app_common.hpp"
#include "mdfh/book_builder.hpp"
#include "mdfh/capture.hpp"
#include "mdfh/replay.hpp"

using namespace mdfh;

int main(int argc, char** argv) {
    app::Args args(argc, argv);
    const std::string in = args.str("in");
    if (in.empty()) { std::fprintf(stderr, "usage: mdfh_replay --in FILE [--mode ...]\n"); return 2; }

    CaptureReader reader;
    if (!reader.open(in)) {
        std::fprintf(stderr, "cannot read capture: %s\n", reader.error().c_str());
        return 1;
    }

    ReplayConfig cfg;
    std::string mode = args.str("mode", "max");
    if (mode == "realtime") cfg.mode = ReplayMode::Realtime;
    else if (mode == "accel") cfg.mode = ReplayMode::Accelerated;
    else cfg.mode = ReplayMode::MaxSpeed;
    cfg.speed = args.f64("speed", 10.0);

    BookBuilder builder;
    ReplayDriver driver(builder);
    ReplayResult r = driver.replay(reader.messages(), cfg);

    std::printf("=== replay complete ===\n");
    std::printf("messages parsed : %llu\n", (unsigned long long)r.messages_parsed);
    std::printf("malformed       : %llu\n", (unsigned long long)r.malformed);
    std::printf("forwarded/drop  : %llu / %llu\n",
                (unsigned long long)r.forwarded, (unsigned long long)r.dropped);
    std::printf("seq in/dup/ooo/gap : %llu / %llu / %llu / %llu (missing %llu)\n",
                (unsigned long long)r.seq.in_order, (unsigned long long)r.seq.duplicates,
                (unsigned long long)r.seq.out_of_order, (unsigned long long)r.seq.gap_events,
                (unsigned long long)r.seq.missing);
    std::printf("symbols         : %zu\n", builder.manager().symbol_count());
    std::printf("book invariants : %s\n", builder.manager().check_all_invariants() ? "OK" : "VIOLATED");
    std::printf("book fingerprint: %016llx\n", (unsigned long long)book_fingerprint(builder.manager()));
    std::printf("wall seconds    : %.4f\n", r.wall_seconds);

    // Print top-of-book for each symbol.
    builder.manager().for_each([&](SymbolId sym, const OrderBook& bk) {
        auto bb = bk.best_bid();
        auto ba = bk.best_ask();
        std::printf("  sym %u: bid %s x%u | ask %s x%u | levels %zu/%zu\n",
                    sym,
                    bb ? std::to_string(price_to_double(bb->price)).c_str() : "-",
                    bb ? bb->quantity : 0,
                    ba ? std::to_string(price_to_double(ba->price)).c_str() : "-",
                    ba ? ba->quantity : 0,
                    bk.bid_levels(), bk.ask_levels());
    });
    return 0;
}
