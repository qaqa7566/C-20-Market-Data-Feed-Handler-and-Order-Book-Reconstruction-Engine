#pragma once

#include "mdfh/protocol.hpp"

// Normalized event sink.
//
// The parser converts wire bytes into typed messages and pushes them to an
// event handler. Two flavours are provided:
//
//  * EventHandler -- a classic abstract base class (virtual dispatch). Convenient
//    when the sink is chosen at runtime.
//  * The HandlerLike concept -- lets the parser be templated on a concrete
//    handler for static dispatch (zero virtual-call overhead on the hot path,
//    which matters for the benchmark).
//
// Keeping this interface tiny is deliberate: it is the seam between the
// transport/parsing layer and all business logic (order book, analytics).
namespace mdfh {

class EventHandler {
public:
    virtual ~EventHandler() = default;

    virtual void on_add_order(const MessageHeader&, const AddOrder&) {}
    virtual void on_cancel_order(const MessageHeader&, const CancelOrder&) {}
    virtual void on_modify_order(const MessageHeader&, const ModifyOrder&) {}
    virtual void on_trade(const MessageHeader&, const Trade&) {}
    virtual void on_snapshot(const MessageHeader&, const BookSnapshot&) {}
    virtual void on_heartbeat(const MessageHeader&) {}

    // Called when a structurally invalid message is encountered.
    virtual void on_malformed(std::size_t /*offset*/) {}
};

// clang-format off
template <typename H>
concept HandlerLike = requires(H h, const MessageHeader& hdr, const AddOrder& a,
                               const CancelOrder& c, const ModifyOrder& m,
                               const Trade& t, const BookSnapshot& s,
                               std::size_t off) {
    h.on_add_order(hdr, a);
    h.on_cancel_order(hdr, c);
    h.on_modify_order(hdr, m);
    h.on_trade(hdr, t);
    h.on_snapshot(hdr, s);
    h.on_heartbeat(hdr);
    h.on_malformed(off);
};
// clang-format on

}  // namespace mdfh
