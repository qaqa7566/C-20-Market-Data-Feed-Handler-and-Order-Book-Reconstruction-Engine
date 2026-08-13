#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "mdfh/byte_order.hpp"
#include "mdfh/protocol.hpp"
#include "mdfh/types.hpp"

// Byte-exact framing for the wire protocol. Every encode/decode routine works
// on a std::span<std::byte> and performs explicit bounds checking; there is no
// reinterpret_cast of the buffer into a struct anywhere in the codebase.
namespace mdfh {

using detail::load_le;
using detail::store_le;

// ---- Header ----------------------------------------------------------------

inline std::byte* encode_header(std::byte* p, MsgType type, Sequence seq,
                                TimestampNs ts) noexcept {
    p = store_le<std::uint8_t>(p, static_cast<std::uint8_t>(type));
    p = store_le<std::uint8_t>(p, kProtocolVersion);
    p = store_le<std::uint16_t>(
        p, static_cast<std::uint16_t>(body_size_for(type)));
    p = store_le<std::uint32_t>(p, seq);
    p = store_le<std::uint64_t>(p, ts);
    return p;
}

// Decode a header from the front of `buf`. Returns false if the buffer is too
// small or the header is structurally invalid.
[[nodiscard]] inline bool decode_header(std::span<const std::byte> buf,
                                        MessageHeader& out) noexcept {
    if (buf.size() < kHeaderWireSize) return false;
    const std::byte* p = buf.data();
    const auto raw_type = load_le<std::uint8_t>(p + 0);
    if (!is_valid_msg_type(raw_type)) return false;
    out.type      = static_cast<MsgType>(raw_type);
    out.version   = load_le<std::uint8_t>(p + 1);
    out.body_size = load_le<std::uint16_t>(p + 2);
    out.sequence  = load_le<std::uint32_t>(p + 4);
    out.timestamp = load_le<std::uint64_t>(p + 8);
    return true;
}

// ---- Body encoders (write body only; caller writes header first) -----------

inline void encode_body(std::byte* p, const AddOrder& m) noexcept {
    p = store_le<std::uint32_t>(p, m.symbol);
    p = store_le<std::uint64_t>(p, m.order_id);
    p = store_le<Side>(p, m.side);
    p = store_le<std::int64_t>(p, m.price);
    store_le<std::uint32_t>(p, m.quantity);
}

inline void encode_body(std::byte* p, const CancelOrder& m) noexcept {
    p = store_le<std::uint32_t>(p, m.symbol);
    store_le<std::uint64_t>(p, m.order_id);
}

inline void encode_body(std::byte* p, const ModifyOrder& m) noexcept {
    p = store_le<std::uint32_t>(p, m.symbol);
    p = store_le<std::uint64_t>(p, m.order_id);
    p = store_le<std::int64_t>(p, m.new_price);
    store_le<std::uint32_t>(p, m.new_quantity);
}

inline void encode_body(std::byte* p, const Trade& m) noexcept {
    p = store_le<std::uint32_t>(p, m.symbol);
    p = store_le<std::uint64_t>(p, m.resting_order_id);
    p = store_le<std::int64_t>(p, m.price);
    p = store_le<std::uint32_t>(p, m.quantity);
    store_le<Side>(p, m.aggressor_side);
}

inline void encode_body(std::byte* p, const BookSnapshot& m) noexcept {
    p = store_le<std::uint32_t>(p, m.symbol);
    p = store_le<std::uint8_t>(p, m.num_bid_levels);
    p = store_le<std::uint8_t>(p, m.num_ask_levels);
    p = store_le<std::uint16_t>(p, 0);  // reserved
    for (const auto& lvl : m.bids) {
        p = store_le<std::int64_t>(p, lvl.price);
        p = store_le<std::uint32_t>(p, lvl.quantity);
    }
    for (const auto& lvl : m.asks) {
        p = store_le<std::int64_t>(p, lvl.price);
        p = store_le<std::uint32_t>(p, lvl.quantity);
    }
}

inline void encode_body(std::byte* /*p*/, const Heartbeat& /*m*/) noexcept {}

// Map a body type to its MsgType at compile time.
template <typename T> struct msg_type_of;
template <> struct msg_type_of<AddOrder>     { static constexpr MsgType value = MsgType::AddOrder; };
template <> struct msg_type_of<CancelOrder>  { static constexpr MsgType value = MsgType::CancelOrder; };
template <> struct msg_type_of<ModifyOrder>  { static constexpr MsgType value = MsgType::ModifyOrder; };
template <> struct msg_type_of<Trade>        { static constexpr MsgType value = MsgType::Trade; };
template <> struct msg_type_of<BookSnapshot> { static constexpr MsgType value = MsgType::BookSnapshot; };
template <> struct msg_type_of<Heartbeat>    { static constexpr MsgType value = MsgType::Heartbeat; };

// Encode a full framed message (header + body) into `out`.
// Returns the number of bytes written, or 0 if `out` is too small.
template <typename Body>
[[nodiscard]] std::size_t encode_message(std::span<std::byte> out, Sequence seq,
                                         TimestampNs ts, const Body& body) {
    constexpr MsgType type = msg_type_of<Body>::value;
    const std::size_t total = kHeaderWireSize + body_size_for(type);
    if (out.size() < total) return 0;
    std::byte* p = encode_header(out.data(), type, seq, ts);
    encode_body(p, body);
    return total;
}

// ---- Body decoders ---------------------------------------------------------
// Each decoder assumes `body` spans exactly the message body (caller validates
// the length against the header first).

[[nodiscard]] inline bool decode_body(std::span<const std::byte> b, AddOrder& m) noexcept {
    if (b.size() < kAddOrderBodySize) return false;
    const std::byte* p = b.data();
    m.symbol   = load_le<std::uint32_t>(p + 0);
    m.order_id = load_le<std::uint64_t>(p + 4);
    m.side     = static_cast<Side>(load_le<std::uint8_t>(p + 12));
    m.price    = load_le<std::int64_t>(p + 13);
    m.quantity = load_le<std::uint32_t>(p + 21);
    return true;
}

[[nodiscard]] inline bool decode_body(std::span<const std::byte> b, CancelOrder& m) noexcept {
    if (b.size() < kCancelOrderBodySize) return false;
    const std::byte* p = b.data();
    m.symbol   = load_le<std::uint32_t>(p + 0);
    m.order_id = load_le<std::uint64_t>(p + 4);
    return true;
}

[[nodiscard]] inline bool decode_body(std::span<const std::byte> b, ModifyOrder& m) noexcept {
    if (b.size() < kModifyOrderBodySize) return false;
    const std::byte* p = b.data();
    m.symbol       = load_le<std::uint32_t>(p + 0);
    m.order_id     = load_le<std::uint64_t>(p + 4);
    m.new_price    = load_le<std::int64_t>(p + 12);
    m.new_quantity = load_le<std::uint32_t>(p + 20);
    return true;
}

[[nodiscard]] inline bool decode_body(std::span<const std::byte> b, Trade& m) noexcept {
    if (b.size() < kTradeBodySize) return false;
    const std::byte* p = b.data();
    m.symbol           = load_le<std::uint32_t>(p + 0);
    m.resting_order_id = load_le<std::uint64_t>(p + 4);
    m.price            = load_le<std::int64_t>(p + 12);
    m.quantity         = load_le<std::uint32_t>(p + 20);
    m.aggressor_side   = static_cast<Side>(load_le<std::uint8_t>(p + 24));
    return true;
}

[[nodiscard]] inline bool decode_body(std::span<const std::byte> b, BookSnapshot& m) noexcept {
    if (b.size() < kSnapshotBodySize) return false;
    const std::byte* p = b.data();
    m.symbol         = load_le<std::uint32_t>(p + 0);
    m.num_bid_levels = load_le<std::uint8_t>(p + 4);
    m.num_ask_levels = load_le<std::uint8_t>(p + 5);
    // bytes 6..7 reserved
    std::size_t off = 8;
    for (auto& lvl : m.bids) {
        lvl.price    = load_le<std::int64_t>(p + off);
        lvl.quantity = load_le<std::uint32_t>(p + off + 8);
        off += 12;
    }
    for (auto& lvl : m.asks) {
        lvl.price    = load_le<std::int64_t>(p + off);
        lvl.quantity = load_le<std::uint32_t>(p + off + 8);
        off += 12;
    }
    return true;
}

[[nodiscard]] inline bool decode_body(std::span<const std::byte> /*b*/, Heartbeat& /*m*/) noexcept {
    return true;
}

}  // namespace mdfh
