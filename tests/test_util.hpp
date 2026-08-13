#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "mdfh/protocol.hpp"
#include "mdfh/serialization.hpp"
#include "mdfh/simulator.hpp"

namespace mdfh::test {

// Build a fully framed in-memory feed from the simulator.
inline std::vector<std::byte> build_sim_feed(std::uint64_t messages,
                                             std::uint32_t symbols,
                                             std::uint64_t seed) {
    SimConfig cfg;
    cfg.num_messages = messages;
    cfg.num_symbols = symbols;
    cfg.seed = seed;
    cfg.heartbeat_interval = 0;  // keep test feeds pure add/cancel/modify/trade
    MarketSimulator sim(cfg);
    std::vector<std::byte> buf;
    std::array<std::byte, kMaxMessageSize> tmp{};
    sim.run([&](Sequence s, TimestampNs t, const auto& body) {
        std::size_t n = encode_message(tmp, s, t, body);
        buf.insert(buf.end(), tmp.data(), tmp.data() + n);
    });
    return buf;
}

// Accumulates framed messages into a byte buffer with auto-incrementing
// sequence numbers, so tests can build a wire stream ergonomically.
class FeedBuilder {
public:
    template <typename Body>
    FeedBuilder& add(const Body& body) {
        return add_seq(seq_++, body);
    }

    // Add with an explicit sequence number (for dup/gap/ooo tests).
    template <typename Body>
    FeedBuilder& add_seq(Sequence seq, const Body& body) {
        std::array<std::byte, kMaxMessageSize> tmp{};
        std::size_t n = encode_message(tmp, seq, ts_, body);
        ts_ += 10;
        buf_.insert(buf_.end(), tmp.data(), tmp.data() + n);
        return *this;
    }

    [[nodiscard]] std::span<const std::byte> bytes() const { return {buf_.data(), buf_.size()}; }
    [[nodiscard]] std::vector<std::byte>& raw() { return buf_; }
    [[nodiscard]] Sequence next_seq() const { return seq_; }

private:
    std::vector<std::byte> buf_;
    Sequence               seq_ = 1;
    TimestampNs            ts_  = 1'000;
};

}  // namespace mdfh::test
