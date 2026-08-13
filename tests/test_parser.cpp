#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "mdfh/parser.hpp"
#include "mdfh/serialization.hpp"
#include "test_util.hpp"

using namespace mdfh;

namespace {
struct Recorder {
    std::vector<AddOrder> adds;
    std::size_t cancels = 0, modifies = 0, trades = 0, snaps = 0, heartbeats = 0, malformed = 0;
    std::vector<std::size_t> malformed_offsets;

    void on_add_order(const MessageHeader&, const AddOrder& m) { adds.push_back(m); }
    void on_cancel_order(const MessageHeader&, const CancelOrder&) { ++cancels; }
    void on_modify_order(const MessageHeader&, const ModifyOrder&) { ++modifies; }
    void on_trade(const MessageHeader&, const Trade&) { ++trades; }
    void on_snapshot(const MessageHeader&, const BookSnapshot&) { ++snaps; }
    void on_heartbeat(const MessageHeader&) { ++heartbeats; }
    void on_malformed(std::size_t off) { ++malformed; malformed_offsets.push_back(off); }
};
}  // namespace

TEST(Parser, ParsesMixedStream) {
    test::FeedBuilder fb;
    fb.add(AddOrder{1, 10, Side::Buy, 100'0000, 5})
      .add(CancelOrder{1, 10})
      .add(Trade{1, 11, 100'0000, 2, Side::Sell})
      .add(Heartbeat{});

    Recorder rec;
    BinaryParser<Recorder> parser(rec);
    ParseResult r = parser.parse(fb.bytes());

    EXPECT_EQ(r.messages, 4u);
    EXPECT_EQ(r.malformed, 0u);
    EXPECT_EQ(r.bytes_consumed, fb.bytes().size());
    ASSERT_EQ(rec.adds.size(), 1u);
    EXPECT_EQ(rec.adds[0].order_id, 10u);
    EXPECT_EQ(rec.cancels, 1u);
    EXPECT_EQ(rec.trades, 1u);
    EXPECT_EQ(rec.heartbeats, 1u);
}

TEST(Parser, LeavesPartialTailUnconsumed) {
    test::FeedBuilder fb;
    fb.add(AddOrder{1, 10, Side::Buy, 100'0000, 5})
      .add(AddOrder{1, 11, Side::Buy, 100'0000, 6});
    auto full = fb.raw();
    // Chop off the last 5 bytes so the second message is incomplete.
    std::span<const std::byte> truncated(full.data(), full.size() - 5);

    Recorder rec;
    BinaryParser<Recorder> parser(rec);
    ParseResult r = parser.parse(truncated);

    EXPECT_EQ(r.messages, 1u);          // only the first fully arrived
    EXPECT_TRUE(r.needs_more);
    EXPECT_EQ(r.bytes_consumed, kHeaderWireSize + kAddOrderBodySize);
}

TEST(Parser, MalformedBodySizeIsSkippedAndResyncs) {
    // Layout: [valid AddOrder][bad frame: AddOrder header w/ body_size=10 + 10
    // bytes][valid Heartbeat]. The parser flags the bad frame, skips exactly
    // 16+10 bytes using the declared length, and still delivers the heartbeat.
    std::vector<std::byte> buf;
    std::array<std::byte, kMaxMessageSize> tmp{};

    std::size_t n = encode_message(tmp, 1, 100, AddOrder{1, 10, Side::Buy, 1, 1});
    buf.insert(buf.end(), tmp.data(), tmp.data() + n);

    constexpr std::size_t kBadBody = 10;
    std::array<std::byte, kHeaderWireSize + kBadBody> bad{};
    encode_header(bad.data(), MsgType::AddOrder, 2, 200);
    bad[2] = std::byte{kBadBody};  // declared body_size (10) != expected (25)
    bad[3] = std::byte{0};
    buf.insert(buf.end(), bad.data(), bad.data() + bad.size());

    n = encode_message(tmp, 3, 300, Heartbeat{});
    buf.insert(buf.end(), tmp.data(), tmp.data() + n);

    Recorder rec;
    BinaryParser<Recorder> parser(rec);
    ParseResult r = parser.parse(buf);

    EXPECT_EQ(rec.adds.size(), 1u);      // first valid message delivered
    EXPECT_EQ(r.malformed, 1u);          // bad frame flagged exactly once
    EXPECT_EQ(rec.heartbeats, 1u);       // resync recovered the heartbeat
}

TEST(Parser, InvalidTypeStopsParsing) {
    std::vector<std::byte> buf;
    std::array<std::byte, kMaxMessageSize> tmp{};
    std::size_t n = encode_message(tmp, 1, 100, Heartbeat{});
    buf.insert(buf.end(), tmp.data(), tmp.data() + n);
    // Append 16 bytes with an invalid type byte.
    std::array<std::byte, kHeaderWireSize> junk{};
    junk[0] = std::byte{250};
    buf.insert(buf.end(), junk.data(), junk.data() + junk.size());

    Recorder rec;
    BinaryParser<Recorder> parser(rec);
    ParseResult r = parser.parse(buf);
    EXPECT_EQ(rec.heartbeats, 1u);
    EXPECT_EQ(r.malformed, 1u);
}

TEST(Parser, VersionMismatchIsMalformed) {
    std::array<std::byte, kHeaderWireSize + kAddOrderBodySize> bad{};
    encode_header(bad.data(), MsgType::AddOrder, 1, 1);
    bad[1] = std::byte{99};  // wrong version
    Recorder rec;
    BinaryParser<Recorder> parser(rec);
    ParseResult r = parser.parse(bad);
    EXPECT_EQ(r.malformed, 1u);
    EXPECT_EQ(rec.adds.size(), 0u);
}
