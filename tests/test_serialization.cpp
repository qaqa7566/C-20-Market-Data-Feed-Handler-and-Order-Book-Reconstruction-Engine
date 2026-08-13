#include <gtest/gtest.h>

#include <array>

#include "mdfh/serialization.hpp"

using namespace mdfh;

namespace {
template <typename Body>
Body round_trip(Sequence seq, TimestampNs ts, const Body& in, MessageHeader& hdr_out) {
    std::array<std::byte, kMaxMessageSize> buf{};
    std::size_t n = encode_message(buf, seq, ts, in);
    EXPECT_EQ(n, kHeaderWireSize + body_size_for(msg_type_of<Body>::value));

    std::span<const std::byte> whole(buf.data(), n);
    EXPECT_TRUE(decode_header(whole, hdr_out));
    EXPECT_EQ(hdr_out.sequence, seq);
    EXPECT_EQ(hdr_out.timestamp, ts);
    EXPECT_EQ(hdr_out.type, msg_type_of<Body>::value);

    Body out;
    EXPECT_TRUE(decode_body(whole.subspan(kHeaderWireSize), out));
    return out;
}
}  // namespace

TEST(Serialization, AddOrderRoundTrip) {
    AddOrder in{42, 1001, Side::Sell, 123'4500, 250};
    MessageHeader h;
    AddOrder out = round_trip(7, 999'000, in, h);
    EXPECT_EQ(out.symbol, in.symbol);
    EXPECT_EQ(out.order_id, in.order_id);
    EXPECT_EQ(out.side, in.side);
    EXPECT_EQ(out.price, in.price);
    EXPECT_EQ(out.quantity, in.quantity);
}

TEST(Serialization, CancelModifyTradeRoundTrip) {
    MessageHeader h;
    CancelOrder c{3, 55};
    auto c2 = round_trip(1, 2, c, h);
    EXPECT_EQ(c2.symbol, 3u);
    EXPECT_EQ(c2.order_id, 55u);

    ModifyOrder m{9, 77, -500, 10};
    auto m2 = round_trip(2, 3, m, h);
    EXPECT_EQ(m2.new_price, -500);
    EXPECT_EQ(m2.new_quantity, 10u);

    Trade t{4, 88, 999'0000, 33, Side::Buy};
    auto t2 = round_trip(3, 4, t, h);
    EXPECT_EQ(t2.resting_order_id, 88u);
    EXPECT_EQ(t2.price, 999'0000);
    EXPECT_EQ(t2.aggressor_side, Side::Buy);
}

TEST(Serialization, SnapshotRoundTrip) {
    BookSnapshot s;
    s.symbol = 5;
    s.num_bid_levels = 2;
    s.num_ask_levels = 1;
    s.bids[0] = {100'0000, 5};
    s.bids[1] = {99'0000, 7};
    s.asks[0] = {101'0000, 3};
    MessageHeader h;
    auto s2 = round_trip(1, 1, s, h);
    EXPECT_EQ(s2.num_bid_levels, 2);
    EXPECT_EQ(s2.num_ask_levels, 1);
    EXPECT_EQ(s2.bids[0].price, 100'0000);
    EXPECT_EQ(s2.bids[1].quantity, 7u);
    EXPECT_EQ(s2.asks[0].price, 101'0000);
}

TEST(Serialization, HeartbeatHasNoBody) {
    std::array<std::byte, kMaxMessageSize> buf{};
    std::size_t n = encode_message(buf, 11, 22, Heartbeat{});
    EXPECT_EQ(n, kHeaderWireSize);
    MessageHeader h;
    EXPECT_TRUE(decode_header({buf.data(), n}, h));
    EXPECT_EQ(h.type, MsgType::Heartbeat);
    EXPECT_EQ(h.body_size, 0);
}

TEST(Serialization, EncodeFailsWhenBufferTooSmall) {
    std::array<std::byte, 4> tiny{};
    EXPECT_EQ(encode_message(tiny, 1, 1, AddOrder{}), 0u);
}

TEST(Serialization, DecodeHeaderRejectsShortBuffer) {
    std::array<std::byte, 8> tiny{};
    MessageHeader h;
    EXPECT_FALSE(decode_header(tiny, h));
}

TEST(Serialization, DecodeHeaderRejectsInvalidType) {
    std::array<std::byte, kHeaderWireSize> buf{};
    buf[0] = std::byte{200};  // not a valid MsgType
    MessageHeader h;
    EXPECT_FALSE(decode_header(buf, h));
}

TEST(Serialization, LittleEndianByteLayout) {
    // Verify the wire really is little-endian, independent of host.
    std::array<std::byte, kMaxMessageSize> buf{};
    (void)encode_message(buf, 0x01020304u, 0, Heartbeat{});
    // sequence occupies bytes [4..7], LE.
    EXPECT_EQ(std::to_integer<int>(buf[4]), 0x04);
    EXPECT_EQ(std::to_integer<int>(buf[5]), 0x03);
    EXPECT_EQ(std::to_integer<int>(buf[6]), 0x02);
    EXPECT_EQ(std::to_integer<int>(buf[7]), 0x01);
}
