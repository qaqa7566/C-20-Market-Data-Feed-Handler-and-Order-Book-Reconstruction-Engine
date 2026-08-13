#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "mdfh/protocol.hpp"
#include "mdfh/types.hpp"

// Single-symbol limit order book with FIFO price-time priority.
//
// Data structures
// ---------------
//   bids_ : std::map<Price, PriceLevel, greater<>>  -> begin() == best bid
//   asks_ : std::map<Price, PriceLevel, less<>>     -> begin() == best ask
//   Each PriceLevel owns a std::list<RestingOrder> in arrival (FIFO) order.
//   orders_ : hash map OrderId -> {side, price, list-iterator} so cancel/modify
//   are O(1) locate + O(1) splice, without scanning the level.
//
// Complexity (L = distinct price levels on a side):
//   add     : O(log L)    (map insert of a new level; O(1) if level exists)
//   cancel  : O(log L)    (locate order O(1), erase level if it empties)
//   modify  : O(log L)
//   execute : O(log L)
//   best/spread : O(1)
//
// Tradeoff: a std::map is a red-black tree -- pointer-chasing and a node
// allocation per new level. For a bounded tick range an array indexed by
// (price - min_price)/tick_size gives O(1) everything and better cache
// behaviour; that is documented in the README as the natural next optimization.
// The map version is chosen first for obvious correctness.
namespace mdfh {

struct RestingOrder {
    OrderId  id{0};
    Quantity quantity{0};
};

struct PriceLevel {
    Price                   price{0};
    Quantity                total_quantity{0};
    std::list<RestingOrder> orders;  // FIFO: front = oldest
};

struct LevelView {
    Price         price{0};
    Quantity      quantity{0};
    std::uint32_t order_count{0};
};

class OrderBook {
public:
    explicit OrderBook(SymbolId symbol = 0) : symbol_(symbol) {}

    [[nodiscard]] SymbolId symbol() const noexcept { return symbol_; }

    // --- Mutations (return false if the referenced order does not exist) ---

    // Insert a new resting order at the back of its price level's FIFO queue.
    // A duplicate order id is ignored (returns false).
    bool add(OrderId id, Side side, Price price, Quantity qty);

    // Remove an order entirely.
    bool cancel(OrderId id);

    // Change an order's price and/or quantity. A pure size *reduction* at the
    // same price keeps time priority; a price change or size *increase* loses
    // priority (the order is re-queued at the back). new_qty == 0 cancels.
    bool modify(OrderId id, Price new_price, Quantity new_qty);

    // Apply an execution against a resting order (partial or full fill).
    bool execute(OrderId resting_id, Quantity qty);

    // --- Queries ---

    [[nodiscard]] bool empty() const noexcept { return orders_.empty(); }
    [[nodiscard]] std::size_t order_count() const noexcept { return orders_.size(); }
    [[nodiscard]] std::size_t bid_levels() const noexcept { return bids_.size(); }
    [[nodiscard]] std::size_t ask_levels() const noexcept { return asks_.size(); }

    [[nodiscard]] std::optional<LevelView> best_bid() const;
    [[nodiscard]] std::optional<LevelView> best_ask() const;

    // Best ask - best bid. Nullopt if either side is empty.
    [[nodiscard]] std::optional<Price> spread() const;

    // Top `depth` levels of a side, best first.
    [[nodiscard]] std::vector<LevelView> bids(std::size_t depth) const;
    [[nodiscard]] std::vector<LevelView> asks(std::size_t depth) const;

    // Build a wire snapshot (top kMaxSnapshotLevels per side).
    [[nodiscard]] BookSnapshot snapshot() const;

    // Validate structural invariants (level totals, ordering, non-crossed).
    // Cheap enough to call after every update in debug/test builds.
    [[nodiscard]] bool check_invariants() const;

    void clear();

private:
    struct OrderLocation {
        Side                              side;
        Price                             price;
        std::list<RestingOrder>::iterator it;
    };

    using BidMap = std::map<Price, PriceLevel, std::greater<>>;
    using AskMap = std::map<Price, PriceLevel, std::less<>>;

    SymbolId symbol_;
    BidMap   bids_;
    AskMap   asks_;
    std::unordered_map<OrderId, OrderLocation> orders_;

    template <typename Map>
    static std::vector<LevelView> top_levels(const Map& m, std::size_t depth);
};

}  // namespace mdfh
