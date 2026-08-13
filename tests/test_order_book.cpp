#include <gtest/gtest.h>

#include "mdfh/order_book.hpp"

using namespace mdfh;

namespace {
constexpr Price px(std::int64_t whole, std::int64_t frac_ten_thousandths = 0) {
    return whole * kPriceScale + frac_ten_thousandths;
}
}  // namespace

TEST(OrderBook, EmptyBookHasNoBestOrSpread) {
    OrderBook b(1);
    EXPECT_TRUE(b.empty());
    EXPECT_FALSE(b.best_bid());
    EXPECT_FALSE(b.best_ask());
    EXPECT_FALSE(b.spread());
    EXPECT_TRUE(b.check_invariants());
}

TEST(OrderBook, AddSetsBestBidAskAndSpread) {
    OrderBook b(1);
    EXPECT_TRUE(b.add(1, Side::Buy, px(100), 5));
    EXPECT_TRUE(b.add(2, Side::Sell, px(101), 3));
    ASSERT_TRUE(b.best_bid());
    ASSERT_TRUE(b.best_ask());
    EXPECT_EQ(b.best_bid()->price, px(100));
    EXPECT_EQ(b.best_bid()->quantity, 5u);
    EXPECT_EQ(b.best_ask()->price, px(101));
    ASSERT_TRUE(b.spread());
    EXPECT_EQ(*b.spread(), px(1));
    EXPECT_TRUE(b.check_invariants());
}

TEST(OrderBook, DuplicateOrderIdRejected) {
    OrderBook b(1);
    EXPECT_TRUE(b.add(1, Side::Buy, px(100), 5));
    EXPECT_FALSE(b.add(1, Side::Buy, px(100), 9));
    EXPECT_EQ(b.order_count(), 1u);
}

TEST(OrderBook, PriceLevelAggregatesQuantityAndOrders) {
    OrderBook b(1);
    b.add(1, Side::Buy, px(100), 5);
    b.add(2, Side::Buy, px(100), 7);  // same level, behind order 1 (FIFO)
    ASSERT_TRUE(b.best_bid());
    EXPECT_EQ(b.best_bid()->quantity, 12u);
    EXPECT_EQ(b.best_bid()->order_count, 2u);
    EXPECT_EQ(b.bid_levels(), 1u);
}

TEST(OrderBook, CancelRemovesOrderAndEmptiesLevel) {
    OrderBook b(1);
    b.add(1, Side::Buy, px(100), 5);
    EXPECT_TRUE(b.cancel(1));
    EXPECT_TRUE(b.empty());
    EXPECT_EQ(b.bid_levels(), 0u);
    EXPECT_FALSE(b.cancel(1));           // already gone
    EXPECT_FALSE(b.cancel(999));         // never existed
}

TEST(OrderBook, ModifyReduceKeepsLevelChangePriceMovesLevel) {
    OrderBook b(1);
    b.add(1, Side::Buy, px(100), 10);
    // Reduce in place.
    EXPECT_TRUE(b.modify(1, px(100), 4));
    EXPECT_EQ(b.best_bid()->quantity, 4u);
    EXPECT_EQ(b.bid_levels(), 1u);
    // Move to a new price level.
    EXPECT_TRUE(b.modify(1, px(99), 4));
    EXPECT_EQ(b.best_bid()->price, px(99));
    EXPECT_EQ(b.bid_levels(), 1u);
    // Modify to zero quantity cancels.
    EXPECT_TRUE(b.modify(1, px(99), 0));
    EXPECT_TRUE(b.empty());
}

TEST(OrderBook, ModifyUnknownReturnsFalse) {
    OrderBook b(1);
    EXPECT_FALSE(b.modify(42, px(100), 1));
}

TEST(OrderBook, PartialAndFullExecution) {
    OrderBook b(1);
    b.add(1, Side::Sell, px(101), 10);
    // Partial fill of 4 -> 6 remain.
    EXPECT_TRUE(b.execute(1, 4));
    EXPECT_EQ(b.best_ask()->quantity, 6u);
    EXPECT_EQ(b.order_count(), 1u);
    // Overfill request caps at remaining and removes the order.
    EXPECT_TRUE(b.execute(1, 100));
    EXPECT_TRUE(b.empty());
    EXPECT_FALSE(b.execute(1, 1));  // gone
}

TEST(OrderBook, FifoOrderPreservedAcrossExecutions) {
    OrderBook b(1);
    b.add(1, Side::Buy, px(100), 5);
    b.add(2, Side::Buy, px(100), 5);
    // Execute against the first resting order fully; the second remains.
    EXPECT_TRUE(b.execute(1, 5));
    EXPECT_EQ(b.best_bid()->order_count, 1u);
    EXPECT_EQ(b.best_bid()->quantity, 5u);
    EXPECT_TRUE(b.check_invariants());
}

TEST(OrderBook, DepthReportsBestFirst) {
    OrderBook b(1);
    b.add(1, Side::Buy, px(100), 1);
    b.add(2, Side::Buy, px(99), 2);
    b.add(3, Side::Buy, px(101), 3);
    auto bids = b.bids(3);
    ASSERT_EQ(bids.size(), 3u);
    EXPECT_EQ(bids[0].price, px(101));  // best (highest) first
    EXPECT_EQ(bids[1].price, px(100));
    EXPECT_EQ(bids[2].price, px(99));

    b.add(4, Side::Sell, px(105), 1);
    b.add(5, Side::Sell, px(103), 1);
    auto asks = b.asks(2);
    ASSERT_EQ(asks.size(), 2u);
    EXPECT_EQ(asks[0].price, px(103));  // best (lowest) first
    EXPECT_EQ(asks[1].price, px(105));
}

TEST(OrderBook, CrossedBookFailsInvariant) {
    OrderBook b(1);
    b.add(1, Side::Buy, px(102), 5);   // bid above ask -> crossed
    b.add(2, Side::Sell, px(101), 5);
    EXPECT_FALSE(b.check_invariants());
}

TEST(OrderBook, SnapshotReflectsTopLevels) {
    OrderBook b(7);
    b.add(1, Side::Buy, px(100), 5);
    b.add(2, Side::Sell, px(101), 3);
    BookSnapshot s = b.snapshot();
    EXPECT_EQ(s.symbol, 7u);
    EXPECT_EQ(s.num_bid_levels, 1);
    EXPECT_EQ(s.num_ask_levels, 1);
    EXPECT_EQ(s.bids[0].price, px(100));
    EXPECT_EQ(s.asks[0].price, px(101));
}
