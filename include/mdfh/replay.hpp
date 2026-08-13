#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "mdfh/book_builder.hpp"
#include "mdfh/feed_handler.hpp"
#include "mdfh/sequence_tracker.hpp"

// Deterministic replay of a captured feed. The same capture replayed at any
// speed produces the identical final book state, because the messages and their
// order are identical -- only the wall-clock pacing differs.
namespace mdfh {

enum class ReplayMode {
    MaxSpeed,     // process as fast as possible (benchmarking)
    Realtime,     // honour inter-message timestamps (1x)
    Accelerated,  // honour timestamps divided by `speed`
};

struct ReplayConfig {
    ReplayMode mode  = ReplayMode::MaxSpeed;
    double     speed = 1.0;  // used when mode == Accelerated
};

struct ReplayResult {
    std::uint64_t messages_parsed = 0;
    std::uint64_t malformed       = 0;
    std::uint64_t forwarded       = 0;
    std::uint64_t dropped         = 0;
    SeqStats      seq{};
    double        wall_seconds    = 0.0;
};

// Drives a BookBuilder from a capture buffer. Concrete (non-template) so it
// lives in the compiled library.
class ReplayDriver {
public:
    explicit ReplayDriver(BookBuilder& builder) : builder_(builder), feed_(builder_) {}

    ReplayResult replay(std::span<const std::byte> messages, const ReplayConfig& cfg);

private:
    BookBuilder&             builder_;
    FeedHandler<BookBuilder> feed_;
};

// Order-independent 64-bit fingerprint of top-of-book state across all symbols.
// Used by tests to assert that two replays converged to identical book state.
[[nodiscard]] std::uint64_t book_fingerprint(const BookManager& mgr);

}  // namespace mdfh
