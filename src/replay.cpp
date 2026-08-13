#include "mdfh/replay.hpp"

#include <chrono>
#include <thread>

#include "mdfh/parser.hpp"
#include "mdfh/serialization.hpp"

namespace mdfh {

ReplayResult ReplayDriver::replay(std::span<const std::byte> messages,
                                  const ReplayConfig& cfg) {
    ReplayResult res;
    const auto t0 = std::chrono::steady_clock::now();

    if (cfg.mode == ReplayMode::MaxSpeed) {
        // Fast path: hand the whole buffer to the feed handler at once.
        ParseResult r = feed_.consume(messages);
        res.messages_parsed = r.messages;
        res.malformed = r.malformed;
    } else {
        // Timed path: walk frame by frame, pacing on message timestamps.
        using namespace std::chrono;
        const auto start_wall = steady_clock::now();
        TimestampNs first_ts = 0;
        bool have_first = false;
        std::size_t off = 0;

        while (off + kHeaderWireSize <= messages.size()) {
            MessageHeader hdr;
            if (!decode_header(messages.subspan(off, kHeaderWireSize), hdr)) break;
            const std::size_t frame = kHeaderWireSize + hdr.body_size;
            if (off + frame > messages.size()) break;

            if (!have_first) { first_ts = hdr.timestamp; have_first = true; }

            // Sleep until this message's scheduled time.
            double scale = (cfg.mode == ReplayMode::Accelerated && cfg.speed > 0)
                               ? cfg.speed : 1.0;
            auto target = start_wall +
                          nanoseconds(static_cast<std::int64_t>(
                              static_cast<double>(hdr.timestamp - first_ts) / scale));
            std::this_thread::sleep_until(target);

            ParseResult r = feed_.consume(messages.subspan(off, frame));
            res.messages_parsed += r.messages;
            res.malformed += r.malformed;
            off += frame;
        }
    }

    res.forwarded = feed_.forwarded();
    res.dropped = feed_.dropped();
    res.seq = feed_.seq_stats();
    res.wall_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

std::uint64_t book_fingerprint(const BookManager& mgr) {
    std::uint64_t acc = 0;
    mgr.for_each([&](SymbolId sym, const OrderBook& bk) {
        // Mix per-symbol state; combine across symbols with addition so the
        // (unordered) iteration order does not affect the result.
        std::uint64_t h = 1469598103934665603ULL;  // FNV offset basis
        auto mix = [&h](std::uint64_t v) {
            h ^= v;
            h *= 1099511628211ULL;  // FNV prime
        };
        mix(sym);
        mix(bk.bid_levels());
        mix(bk.ask_levels());
        mix(bk.order_count());
        if (auto bb = bk.best_bid()) {
            mix(static_cast<std::uint64_t>(bb->price));
            mix(bb->quantity);
            mix(bb->order_count);
        }
        if (auto ba = bk.best_ask()) {
            mix(static_cast<std::uint64_t>(ba->price));
            mix(ba->quantity);
            mix(ba->order_count);
        }
        acc += h;
    });
    return acc;
}

}  // namespace mdfh
