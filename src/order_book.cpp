#include "mdfh/order_book.hpp"

#include <algorithm>

namespace mdfh {
namespace {

// Insert an order at the back of its price level in `m`, creating the level if
// necessary. Returns the list iterator to the new order.
template <typename Map>
std::list<RestingOrder>::iterator
insert_order(Map& m, Price price, OrderId id, Quantity qty) {
    auto [it, inserted] = m.try_emplace(price);
    PriceLevel& level = it->second;
    if (inserted) level.price = price;
    level.total_quantity += qty;
    level.orders.push_back(RestingOrder{id, qty});
    return std::prev(level.orders.end());
}

// Remove one order from its level in `m`; erase the level if it becomes empty.
template <typename Map>
void erase_order(Map& m, Price price,
                 std::list<RestingOrder>::iterator order_it) {
    auto lvl = m.find(price);
    if (lvl == m.end()) return;  // should not happen given a valid locator
    PriceLevel& level = lvl->second;
    level.total_quantity -= order_it->quantity;
    level.orders.erase(order_it);
    if (level.orders.empty()) m.erase(lvl);
}

}  // namespace

bool OrderBook::add(OrderId id, Side side, Price price, Quantity qty) {
    if (qty == 0) return false;
    if (orders_.find(id) != orders_.end()) return false;  // duplicate id

    std::list<RestingOrder>::iterator it =
        (side == Side::Buy) ? insert_order(bids_, price, id, qty)
                            : insert_order(asks_, price, id, qty);
    orders_.emplace(id, OrderLocation{side, price, it});
    return true;
}

bool OrderBook::cancel(OrderId id) {
    auto found = orders_.find(id);
    if (found == orders_.end()) return false;
    const OrderLocation& loc = found->second;
    if (loc.side == Side::Buy) erase_order(bids_, loc.price, loc.it);
    else                       erase_order(asks_, loc.price, loc.it);
    orders_.erase(found);
    return true;
}

bool OrderBook::modify(OrderId id, Price new_price, Quantity new_qty) {
    if (new_qty == 0) return cancel(id);

    auto found = orders_.find(id);
    if (found == orders_.end()) return false;
    OrderLocation& loc = found->second;

    const Quantity old_qty = loc.it->quantity;

    // Fast path: same price and a size reduction keeps time priority.
    if (new_price == loc.price && new_qty <= old_qty) {
        const Quantity delta = old_qty - new_qty;
        loc.it->quantity = new_qty;
        if (loc.side == Side::Buy) bids_.find(loc.price)->second.total_quantity -= delta;
        else                       asks_.find(loc.price)->second.total_quantity -= delta;
        return true;
    }

    // Otherwise: re-queue at the back of the (possibly new) level -> loses
    // priority, matching real exchange semantics for price changes / size-ups.
    const Side side = loc.side;
    if (side == Side::Buy) {
        erase_order(bids_, loc.price, loc.it);
        auto it = insert_order(bids_, new_price, id, new_qty);
        loc = OrderLocation{side, new_price, it};
    } else {
        erase_order(asks_, loc.price, loc.it);
        auto it = insert_order(asks_, new_price, id, new_qty);
        loc = OrderLocation{side, new_price, it};
    }
    return true;
}

bool OrderBook::execute(OrderId resting_id, Quantity qty) {
    auto found = orders_.find(resting_id);
    if (found == orders_.end()) return false;
    OrderLocation& loc = found->second;

    const Quantity fill = std::min(qty, loc.it->quantity);
    loc.it->quantity -= fill;

    if (loc.side == Side::Buy) bids_.find(loc.price)->second.total_quantity -= fill;
    else                       asks_.find(loc.price)->second.total_quantity -= fill;

    if (loc.it->quantity == 0) {
        if (loc.side == Side::Buy) erase_order(bids_, loc.price, loc.it);
        else                       erase_order(asks_, loc.price, loc.it);
        orders_.erase(found);
    }
    return true;
}

std::optional<LevelView> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    const auto& lvl = bids_.begin()->second;
    return LevelView{lvl.price, lvl.total_quantity,
                     static_cast<std::uint32_t>(lvl.orders.size())};
}

std::optional<LevelView> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    const auto& lvl = asks_.begin()->second;
    return LevelView{lvl.price, lvl.total_quantity,
                     static_cast<std::uint32_t>(lvl.orders.size())};
}

std::optional<Price> OrderBook::spread() const {
    if (bids_.empty() || asks_.empty()) return std::nullopt;
    return asks_.begin()->second.price - bids_.begin()->second.price;
}

template <typename Map>
std::vector<LevelView> OrderBook::top_levels(const Map& m, std::size_t depth) {
    std::vector<LevelView> out;
    out.reserve(std::min(depth, m.size()));
    for (const auto& [price, lvl] : m) {
        if (out.size() >= depth) break;
        out.push_back(LevelView{lvl.price, lvl.total_quantity,
                                static_cast<std::uint32_t>(lvl.orders.size())});
    }
    return out;
}

std::vector<LevelView> OrderBook::bids(std::size_t depth) const {
    return top_levels(bids_, depth);
}

std::vector<LevelView> OrderBook::asks(std::size_t depth) const {
    return top_levels(asks_, depth);
}

BookSnapshot OrderBook::snapshot() const {
    BookSnapshot s;
    s.symbol = symbol_;
    auto b = bids(kMaxSnapshotLevels);
    auto a = asks(kMaxSnapshotLevels);
    s.num_bid_levels = static_cast<std::uint8_t>(b.size());
    s.num_ask_levels = static_cast<std::uint8_t>(a.size());
    for (std::size_t i = 0; i < b.size(); ++i) s.bids[i] = {b[i].price, b[i].quantity};
    for (std::size_t i = 0; i < a.size(); ++i) s.asks[i] = {a[i].price, a[i].quantity};
    return s;
}

bool OrderBook::check_invariants() const {
    auto level_ok = [](const auto& m) {
        for (const auto& [price, lvl] : m) {
            if (lvl.orders.empty()) return false;
            if (lvl.price != price) return false;
            Quantity sum = 0;
            for (const auto& o : lvl.orders) {
                if (o.quantity == 0) return false;
                sum += o.quantity;
            }
            if (sum != lvl.total_quantity) return false;
        }
        return true;
    };
    if (!level_ok(bids_) || !level_ok(asks_)) return false;

    // Book must not be crossed: best bid strictly below best ask.
    if (!bids_.empty() && !asks_.empty()) {
        if (bids_.begin()->second.price >= asks_.begin()->second.price) return false;
    }

    // The locator count must match the number of live orders on both sides.
    std::size_t counted = 0;
    for (const auto& [p, lvl] : bids_) counted += lvl.orders.size();
    for (const auto& [p, lvl] : asks_) counted += lvl.orders.size();
    return counted == orders_.size();
}

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
    orders_.clear();
}

}  // namespace mdfh
