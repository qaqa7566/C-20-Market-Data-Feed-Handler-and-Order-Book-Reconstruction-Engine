#pragma once

#include <cstddef>
#include <span>

#include "mdfh/event_handler.hpp"
#include "mdfh/protocol.hpp"
#include "mdfh/serialization.hpp"

// Streaming binary parser.
//
// parse() consumes as many *complete* framed messages as it can from the front
// of a buffer and dispatches each to the handler. It never allocates and never
// reads out of bounds. A trailing partial message (common with TCP) is left
// unconsumed so the caller can append more bytes and re-parse.
namespace mdfh {

struct ParseResult {
    std::size_t bytes_consumed = 0;  // safe to discard from the front of buf
    std::size_t messages       = 0;  // well-formed messages dispatched
    std::size_t malformed      = 0;  // structurally invalid messages seen
    bool        needs_more     = false;  // a partial message tail remains
};

template <HandlerLike H>
class BinaryParser {
public:
    explicit BinaryParser(H& handler) noexcept : handler_(handler) {}

    // Parse messages from `buf`. See ParseResult for the accounting returned.
    //
    // Framing policy: a message is (16-byte header) + (body of the exact size
    // implied by the type). A header whose declared body_size disagrees with
    // its type, or whose type byte is invalid, is treated as malformed. Because
    // the framing is self-describing per message, a malformed *body length* is
    // still recoverable -- we skip the header-declared span and resync. An
    // unparseable header, however, means we cannot know where the next message
    // begins, so we stop.
    ParseResult parse(std::span<const std::byte> buf) {
        ParseResult r;
        std::size_t off = 0;
        while (off + kHeaderWireSize <= buf.size()) {
            MessageHeader hdr;
            auto header_span = buf.subspan(off, kHeaderWireSize);
            if (!decode_header(header_span, hdr)) {
                // Cannot trust framing beyond this point.
                handler_.on_malformed(off);
                ++r.malformed;
                r.bytes_consumed = off + kHeaderWireSize;  // skip the bad header
                return r;
            }

            const std::size_t expected_body = body_size_for(hdr.type);
            const std::size_t frame = kHeaderWireSize + hdr.body_size;

            // Incomplete tail: wait for more bytes.
            if (off + frame > buf.size()) {
                r.needs_more = true;
                break;
            }

            if (hdr.body_size != expected_body || hdr.version != kProtocolVersion) {
                // Body length or version disagrees with the declared type.
                handler_.on_malformed(off);
                ++r.malformed;
                off += frame;  // trust the declared length to resync
                continue;
            }

            auto body = buf.subspan(off + kHeaderWireSize, expected_body);
            dispatch(hdr, body, off, r);
            off += frame;
        }
        r.bytes_consumed = off;
        return r;
    }

private:
    void dispatch(const MessageHeader& hdr, std::span<const std::byte> body,
                  std::size_t off, ParseResult& r) {
        switch (hdr.type) {
            case MsgType::AddOrder: {
                AddOrder m;
                if (decode_body(body, m)) { handler_.on_add_order(hdr, m); ++r.messages; }
                else { handler_.on_malformed(off); ++r.malformed; }
                break;
            }
            case MsgType::CancelOrder: {
                CancelOrder m;
                if (decode_body(body, m)) { handler_.on_cancel_order(hdr, m); ++r.messages; }
                else { handler_.on_malformed(off); ++r.malformed; }
                break;
            }
            case MsgType::ModifyOrder: {
                ModifyOrder m;
                if (decode_body(body, m)) { handler_.on_modify_order(hdr, m); ++r.messages; }
                else { handler_.on_malformed(off); ++r.malformed; }
                break;
            }
            case MsgType::Trade: {
                Trade m;
                if (decode_body(body, m)) { handler_.on_trade(hdr, m); ++r.messages; }
                else { handler_.on_malformed(off); ++r.malformed; }
                break;
            }
            case MsgType::BookSnapshot: {
                BookSnapshot m;
                if (decode_body(body, m)) { handler_.on_snapshot(hdr, m); ++r.messages; }
                else { handler_.on_malformed(off); ++r.malformed; }
                break;
            }
            case MsgType::Heartbeat:
                handler_.on_heartbeat(hdr);
                ++r.messages;
                break;
        }
    }

    H& handler_;
};

}  // namespace mdfh
