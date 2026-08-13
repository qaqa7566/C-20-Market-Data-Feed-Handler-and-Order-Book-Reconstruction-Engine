#pragma once

#include <cstdint>

#include "mdfh/types.hpp"

// Per-feed sequence-number validation.
//
// A real UDP market-data feed can duplicate, drop, or reorder packets (A/B line
// arbitration, network loss, etc.). This tracker classifies each arriving
// sequence number without reordering the stream -- reordering/recovery is a
// policy decision that belongs to a higher layer (request a retransmit, fail
// over to a snapshot feed, ...). Keeping it classification-only makes the
// component trivially testable and side-effect free.
namespace mdfh {

enum class SeqStatus {
    InOrder,      // seq == next expected
    Duplicate,    // an immediate re-send of the last sequence
    Gap,          // seq jumped forward; one or more messages are missing
    OutOfOrder,   // seq is behind the high-water mark (e.g. fills a prior gap)
};

struct SeqStats {
    std::uint64_t total        = 0;  // observations
    std::uint64_t in_order     = 0;
    std::uint64_t duplicates   = 0;
    std::uint64_t out_of_order = 0;
    std::uint64_t gap_events   = 0;  // number of forward jumps
    std::uint64_t missing      = 0;  // total sequence numbers skipped over
};

class SequenceTracker {
public:
    SeqStatus observe(Sequence seq) noexcept {
        ++stats_.total;
        SeqStatus status;
        if (!initialized_) {
            initialized_   = true;
            next_expected_ = seq + 1;
            last_seq_      = seq;
            ++stats_.in_order;
            return SeqStatus::InOrder;
        }
        if (seq == next_expected_) {
            next_expected_ = seq + 1;
            ++stats_.in_order;
            status = SeqStatus::InOrder;
        } else if (seq > next_expected_) {
            stats_.missing += (seq - next_expected_);
            ++stats_.gap_events;
            next_expected_ = seq + 1;
            status = SeqStatus::Gap;
        } else if (seq == last_seq_) {
            ++stats_.duplicates;
            status = SeqStatus::Duplicate;
        } else {
            ++stats_.out_of_order;
            status = SeqStatus::OutOfOrder;
        }
        last_seq_ = seq;
        return status;
    }

    [[nodiscard]] const SeqStats& stats() const noexcept { return stats_; }
    [[nodiscard]] Sequence next_expected() const noexcept { return next_expected_; }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

    void reset() noexcept { *this = SequenceTracker{}; }

private:
    bool     initialized_  = false;
    Sequence next_expected_ = 0;
    Sequence last_seq_      = 0;
    SeqStats stats_{};
};

}  // namespace mdfh
