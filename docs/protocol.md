# Wire Protocol Specification

All integers are **little-endian**. There is no text/JSON on the market-data
path. Prices are scaled 64-bit integers (fixed point): a raw price `P`
represents `P / 10000` (four implied decimals). Quantities are unsigned 32-bit.
Symbols are referenced by a compact 32-bit id (compare the "stock locate" code
in Nasdaq ITCH).

## Message header (16 bytes)

| Offset | Size | Field        | Type   | Notes                                   |
|-------:|-----:|--------------|--------|-----------------------------------------|
| 0      | 1    | `msg_type`   | u8     | see table below                         |
| 1      | 1    | `version`    | u8     | protocol version (currently 1)          |
| 2      | 2    | `body_size`  | u16    | bytes of body following the header      |
| 4      | 4    | `sequence`   | u32    | per-feed monotonically increasing       |
| 8      | 8    | `timestamp`  | u64    | nanoseconds since an arbitrary epoch    |

The body that follows is a fixed size determined by `msg_type`. `body_size` is
redundant with the type and is validated against it; a mismatch is treated as a
malformed message.

## Message types

| Value | Type          | Body size |
|------:|---------------|----------:|
| 0     | Heartbeat     | 0         |
| 1     | AddOrder      | 25        |
| 2     | CancelOrder   | 12        |
| 3     | ModifyOrder   | 24        |
| 4     | Trade         | 25        |
| 5     | BookSnapshot  | 128       |

### AddOrder (25 bytes)

| Offset | Size | Field      | Type |
|-------:|-----:|------------|------|
| 0      | 4    | `symbol`   | u32  |
| 4      | 8    | `order_id` | u64  |
| 12     | 1    | `side`     | u8 (0=Buy, 1=Sell) |
| 13     | 8    | `price`    | i64 (scaled) |
| 21     | 4    | `quantity` | u32  |

### CancelOrder (12 bytes)

`symbol` (u32) + `order_id` (u64).

### ModifyOrder (24 bytes)

`symbol` (u32) + `order_id` (u64) + `new_price` (i64) + `new_quantity` (u32).
Semantics: a same-price size *reduction* keeps time priority; a price change or
size *increase* re-queues at the back of the level; `new_quantity == 0` cancels.

### Trade (25 bytes)

`symbol` (u32) + `resting_order_id` (u64) + `price` (i64) + `quantity` (u32) +
`aggressor_side` (u8). Consumes liquidity from the named resting order.

### BookSnapshot (128 bytes)

`symbol` (u32) + `num_bid_levels` (u8) + `num_ask_levels` (u8) + 2 reserved
bytes, followed by 5 bid levels then 5 ask levels, each `price` (i64) +
`quantity` (u32). Levels beyond the declared counts are zero-filled.

## Capture file format

A capture is a 24-byte header — magic `"MDFHCAP1"` (8 bytes), `version` (u32),
`reserved` (u32), `message_count` (u64) — followed by framed messages exactly as
they would appear on the wire. Replaying a capture therefore exercises the
identical parse and reconstruction path as a live feed.
