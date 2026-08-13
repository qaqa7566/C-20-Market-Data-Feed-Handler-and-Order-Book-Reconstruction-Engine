#include <gtest/gtest.h>

#include "mdfh/book_builder.hpp"

using namespace mdfh;

namespace {
constexpr Price px(std::int64_t whole) { return whole * kPriceScale; }
const MessageHeader kHdr{};
}  // namespace

TEST(BookBuilder, AppliesEventsAcrossSymbols) {
    BookBuilder bb;
    bb.set_verify(true);
    bb.on_add_order(kHdr, AddOrder{1, 10, Side::Buy, px(100), 5});
    bb.on_add_order(kHdr, AddOrder{1, 11, Side::Sell, px(101), 4});
    bb.on_add_order(kHdr, AddOrder{2, 20, Side::Buy, px(50), 8});

    EXPECT_EQ(bb.manager().symbol_count(), 2u);
    const OrderBook* b1 = bb.manager().find(1);
    ASSERT_NE(b1, nullptr);
    EXPECT_EQ(b1->best_bid()->price, px(100));
    EXPECT_EQ(b1->best_ask()->price, px(101));
    EXPECT_TRUE(bb.manager().check_all_invariants());
    EXPECT_EQ(bb.stats().adds, 3u);
}

TEST(BookBuilder, CancelModifyTradeFlow) {
    BookBuilder bb;
    bb.on_add_order(kHdr, AddOrder{1, 10, Side::Buy, px(100), 5});
    bb.on_modify_order(kHdr, ModifyOrder{1, 10, px(100), 3});
    bb.on_trade(kHdr, Trade{1, 10, px(100), 1, Side::Sell});
    EXPECT_EQ(bb.manager().find(1)->best_bid()->quantity, 2u);
    bb.on_cancel_order(kHdr, CancelOrder{1, 10});
    EXPECT_TRUE(bb.manager().find(1)->empty());
    EXPECT_EQ(bb.stats().rejected, 0u);
}

TEST(BookBuilder, RejectsUnknownOrderReferences) {
    BookBuilder bb;
    bb.on_cancel_order(kHdr, CancelOrder{1, 999});
    bb.on_modify_order(kHdr, ModifyOrder{1, 999, px(1), 1});
    bb.on_trade(kHdr, Trade{1, 999, px(1), 1, Side::Buy});
    EXPECT_EQ(bb.stats().rejected, 3u);
}

TEST(BookBuilder, CountsHeartbeatsAndMalformed) {
    BookBuilder bb;
    bb.on_heartbeat(kHdr);
    bb.on_malformed(0);
    EXPECT_EQ(bb.stats().heartbeats, 1u);
    EXPECT_EQ(bb.stats().malformed, 1u);
}
