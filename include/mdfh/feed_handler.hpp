#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "mdfh/event_handler.hpp"
#include "mdfh/parser.hpp"
#include "mdfh/sequence_tracker.hpp"

// FeedHandler sits between the raw transport and the business logic. It:
//   1. reassembles framed messages from a byte stream (TCP) or datagrams (UDP),
//   2. validates sequence numbers (dup/gap/out-of-order),
//   3. forwards accepted, in-order-enough events to a downstream HandlerLike.
//
// Delivery policy (documented, deliberately simple, no reordering here):
//   InOrder / Gap -> forward (a gap is logged; missing messages are lost in
//                    this forward-only model -- recovery is a higher-layer
//                    concern), Duplicate / OutOfOrder -> drop.
//
// It is itself a HandlerLike so the internal parser can call back into it.
namespace mdfh {

template <HandlerLike Down>
class FeedHandler {
public:
    explicit FeedHandler(Down& down) : down_(down) {}

    FeedHandler(const FeedHandler&) = delete;
    FeedHandler& operator=(const FeedHandler&) = delete;

    // Stream entry point (e.g. a TCP segment). A trailing partial message is
    // buffered internally and completed on the next call.
    ParseResult consume(std::span<const std::byte> bytes) {
        BinaryParser<FeedHandler> parser(*this);
        if (residual_.empty()) {
            ParseResult r = parser.parse(bytes);
            if (r.bytes_consumed < bytes.size()) {
                auto tail = bytes.subspan(r.bytes_consumed);
                residual_.assign(tail.begin(), tail.end());
            }
            accumulate(r);
            return r;
        }
        residual_.insert(residual_.end(), bytes.begin(), bytes.end());
        ParseResult r = parser.parse(residual_);
        residual_.erase(residual_.begin(),
                        residual_.begin() +
                            static_cast<std::ptrdiff_t>(r.bytes_consumed));
        accumulate(r);
        return r;
    }

    // Datagram entry point (e.g. a UDP packet): each datagram is self-contained;
    // no residual is carried across calls (a truncated datagram is malformed).
    ParseResult consume_datagram(std::span<const std::byte> bytes) {
        BinaryParser<FeedHandler> parser(*this);
        ParseResult r = parser.parse(bytes);
        accumulate(r);
        return r;
    }

    // --- HandlerLike interface (invoked by the internal parser) ---
    void on_add_order(const MessageHeader& h, const AddOrder& m) {
        if (accept(h.sequence)) down_.on_add_order(h, m);
    }
    void on_cancel_order(const MessageHeader& h, const CancelOrder& m) {
        if (accept(h.sequence)) down_.on_cancel_order(h, m);
    }
    void on_modify_order(const MessageHeader& h, const ModifyOrder& m) {
        if (accept(h.sequence)) down_.on_modify_order(h, m);
    }
    void on_trade(const MessageHeader& h, const Trade& m) {
        if (accept(h.sequence)) down_.on_trade(h, m);
    }
    void on_snapshot(const MessageHeader& h, const BookSnapshot& m) {
        if (accept(h.sequence)) down_.on_snapshot(h, m);
    }
    void on_heartbeat(const MessageHeader& h) {
        if (accept(h.sequence)) down_.on_heartbeat(h);
    }
    void on_malformed(std::size_t off) { down_.on_malformed(off); }

    // --- Accessors ---
    [[nodiscard]] const SeqStats& seq_stats() const noexcept { return seq_.stats(); }
    [[nodiscard]] std::uint64_t forwarded() const noexcept { return forwarded_; }
    [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_; }
    [[nodiscard]] std::uint64_t malformed() const noexcept { return malformed_; }
    [[nodiscard]] std::size_t pending_bytes() const noexcept { return residual_.size(); }

private:
    bool accept(Sequence seq) {
        switch (seq_.observe(seq)) {
            case SeqStatus::InOrder:
            case SeqStatus::Gap:
                ++forwarded_;
                return true;
            case SeqStatus::Duplicate:
            case SeqStatus::OutOfOrder:
                ++dropped_;
                return false;
        }
        return false;
    }

    void accumulate(const ParseResult& r) { malformed_ += r.malformed; }

    Down&                  down_;
    SequenceTracker        seq_;
    std::vector<std::byte> residual_;
    std::uint64_t          forwarded_ = 0;
    std::uint64_t          dropped_   = 0;
    std::uint64_t          malformed_ = 0;
};

}  // namespace mdfh
