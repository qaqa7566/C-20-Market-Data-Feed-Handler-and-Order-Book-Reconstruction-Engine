#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "mdfh/types.hpp"

// Binary market-data wire protocol.
//
// Every message on the wire is a fixed 16-byte header followed by a
// message-type-specific, fixed-size body. All integers are little-endian
// (see byte_order.hpp). The layout is documented in docs/protocol.md.
//
//   +-------------------- MessageHeader (16 bytes) --------------------+
//   | msg_type (u8) | version (u8) | body_size (u16) | sequence (u32)  |
//   |                       timestamp_ns (u64)                         |
//   +-----------------------------------------------------------------+
//
// The structs below are the *decoded* in-memory representation. They are plain
// aggregates; the byte-exact framing lives in serialization.hpp so that the
// in-memory types are never assumed to match the wire layout.
namespace mdfh {

inline constexpr std::uint8_t kProtocolVersion = 1;

enum class MsgType : std::uint8_t {
    Heartbeat    = 0,
    AddOrder     = 1,
    CancelOrder  = 2,
    ModifyOrder  = 3,
    Trade        = 4,
    BookSnapshot = 5,
};

// ---- Header ----------------------------------------------------------------

struct MessageHeader {
    MsgType     type{MsgType::Heartbeat};
    std::uint8_t version{kProtocolVersion};
    std::uint16_t body_size{0};
    Sequence    sequence{0};
    TimestampNs timestamp{0};
};
inline constexpr std::size_t kHeaderWireSize = 16;

// ---- Message bodies --------------------------------------------------------
// The wire body size for each message (used for framing and validation).

struct AddOrder {
    SymbolId symbol{0};
    OrderId  order_id{0};
    Side     side{Side::Buy};
    Price    price{0};
    Quantity quantity{0};
};
inline constexpr std::size_t kAddOrderBodySize = 25;  // 4+8+1+8+4

struct CancelOrder {
    SymbolId symbol{0};
    OrderId  order_id{0};
};
inline constexpr std::size_t kCancelOrderBodySize = 12;  // 4+8

struct ModifyOrder {
    SymbolId symbol{0};
    OrderId  order_id{0};
    Price    new_price{0};
    Quantity new_quantity{0};
};
inline constexpr std::size_t kModifyOrderBodySize = 24;  // 4+8+8+4

struct Trade {
    SymbolId symbol{0};
    OrderId  resting_order_id{0};  // the passive order that was hit
    Price    price{0};
    Quantity quantity{0};
    Side     aggressor_side{Side::Buy};
};
inline constexpr std::size_t kTradeBodySize = 25;  // 4+8+8+4+1

// A book snapshot carries up to kMaxSnapshotLevels levels per side. The body
// is fixed-size (levels beyond `num_levels` are zero-filled) to keep framing
// trivial; a production feed would use a variable-length body instead.
inline constexpr std::size_t kMaxSnapshotLevels = 5;

struct SnapshotLevel {
    Price    price{0};
    Quantity quantity{0};
};

struct BookSnapshot {
    SymbolId symbol{0};
    std::uint8_t num_bid_levels{0};
    std::uint8_t num_ask_levels{0};
    std::array<SnapshotLevel, kMaxSnapshotLevels> bids{};
    std::array<SnapshotLevel, kMaxSnapshotLevels> asks{};
};
// symbol(4) + counts(2) + reserved(2) + 2*levels*(price8+qty4)
inline constexpr std::size_t kSnapshotBodySize =
    8 + 2 * kMaxSnapshotLevels * 12;

struct Heartbeat {};
inline constexpr std::size_t kHeartbeatBodySize = 0;

// Largest possible framed message, used to size stack/ring buffers.
inline constexpr std::size_t kMaxMessageSize =
    kHeaderWireSize + kSnapshotBodySize;

[[nodiscard]] constexpr std::size_t body_size_for(MsgType t) noexcept {
    switch (t) {
        case MsgType::Heartbeat:    return kHeartbeatBodySize;
        case MsgType::AddOrder:     return kAddOrderBodySize;
        case MsgType::CancelOrder:  return kCancelOrderBodySize;
        case MsgType::ModifyOrder:  return kModifyOrderBodySize;
        case MsgType::Trade:        return kTradeBodySize;
        case MsgType::BookSnapshot: return kSnapshotBodySize;
    }
    return 0;
}

[[nodiscard]] constexpr bool is_valid_msg_type(std::uint8_t v) noexcept {
    return v <= static_cast<std::uint8_t>(MsgType::BookSnapshot);
}

}  // namespace mdfh
