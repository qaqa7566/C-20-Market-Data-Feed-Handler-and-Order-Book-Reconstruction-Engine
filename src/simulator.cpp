#include "mdfh/simulator.hpp"

#include <algorithm>

namespace mdfh {

MarketSimulator::MarketSimulator(SimConfig cfg)
    : cfg_(cfg), rng_(cfg.seed), symbols_(cfg.num_symbols), ts_(cfg.ts_start) {
    for (std::uint32_t i = 0; i < cfg_.num_symbols; ++i) {
        symbols_[i].ref_mid =
            cfg_.start_price + static_cast<Price>(i) * cfg_.symbol_price_step;
    }
}

void MarketSimulator::add_depth(SymbolState& s, Side side, Price p, Quantity q) {
    (side == Side::Buy ? s.bid_qty : s.ask_qty)[p] += q;
}

void MarketSimulator::remove_depth(SymbolState& s, Side side, Price p, Quantity q) {
    auto& m = (side == Side::Buy ? s.bid_qty : s.ask_qty);
    auto it = m.find(p);
    if (it == m.end()) return;
    if (it->second <= q) m.erase(it);
    else it->second -= q;
}

void MarketSimulator::register_order(SymbolState& s, OrderId id, Side side,
                                     Price p, Quantity q) {
    s.orders.emplace(id, LiveOrder{side, p, q});
    s.index.emplace(id, s.live_ids.size());
    s.live_ids.push_back(id);
    add_depth(s, side, p, q);
}

void MarketSimulator::unregister_order(SymbolState& s, OrderId id) {
    auto it = s.orders.find(id);
    if (it == s.orders.end()) return;
    remove_depth(s, it->second.side, it->second.price, it->second.qty);
    // Swap-remove from live_ids for O(1) erase.
    std::size_t slot = s.index[id];
    OrderId last = s.live_ids.back();
    s.live_ids[slot] = last;
    s.index[last] = slot;
    s.live_ids.pop_back();
    s.index.erase(id);
    s.orders.erase(it);
}

OrderId MarketSimulator::random_live(SymbolState& s) {
    std::uniform_int_distribution<std::size_t> d(0, s.live_ids.size() - 1);
    return s.live_ids[d(rng_)];
}

Quantity MarketSimulator::random_qty() {
    std::uniform_int_distribution<Quantity> d(cfg_.min_qty, cfg_.max_qty);
    return d(rng_);
}

Price MarketSimulator::choose_price(const SymbolState& s, Side side) {
    std::uniform_int_distribution<std::uint32_t> off_dist(0, cfg_.band_levels - 1);
    const Price tick = cfg_.tick;
    if (side == Side::Buy) {
        const Price ceil = s.ask_qty.empty() ? (s.ref_mid - tick)
                                             : (s.ask_qty.begin()->first - tick);
        Price anchor = ceil;
        if (!s.bid_qty.empty())
            anchor = std::min(s.bid_qty.rbegin()->first + tick, ceil);
        Price price = anchor - static_cast<Price>(off_dist(rng_)) * tick;
        return std::max<Price>(price, tick);
    } else {
        const Price floor = s.bid_qty.empty() ? (s.ref_mid + tick)
                                              : (s.bid_qty.rbegin()->first + tick);
        Price anchor = floor;
        if (!s.ask_qty.empty())
            anchor = std::max(s.ask_qty.begin()->first - tick, floor);
        Price price = anchor + static_cast<Price>(off_dist(rng_)) * tick;
        return price;
    }
}

void MarketSimulator::fill_add(GeneratedMessage& out, SymbolId sym, SymbolState& s) {
    std::uniform_int_distribution<int> side_dist(0, 1);
    Side side = side_dist(rng_) == 0 ? Side::Buy : Side::Sell;
    Price price = choose_price(s, side);
    Quantity qty = random_qty();
    OrderId id = next_order_id_++;
    register_order(s, id, side, price, qty);

    out.type = MsgType::AddOrder;
    out.add = AddOrder{sym, id, side, price, qty};
}

bool MarketSimulator::fill_cancel(GeneratedMessage& out, SymbolId sym, SymbolState& s) {
    if (s.live_ids.empty()) return false;
    OrderId id = random_live(s);
    unregister_order(s, id);
    out.type = MsgType::CancelOrder;
    out.cancel = CancelOrder{sym, id};
    return true;
}

bool MarketSimulator::fill_modify(GeneratedMessage& out, SymbolId sym, SymbolState& s) {
    if (s.live_ids.empty()) return false;
    OrderId id = random_live(s);
    LiveOrder& o = s.orders.at(id);
    // Remove old depth, pick a new price/qty on the same side, re-add.
    remove_depth(s, o.side, o.price, o.qty);
    Price new_price = choose_price(s, o.side);
    Quantity new_qty = random_qty();
    o.price = new_price;
    o.qty = new_qty;
    add_depth(s, o.side, new_price, new_qty);

    out.type = MsgType::ModifyOrder;
    out.modify = ModifyOrder{sym, id, new_price, new_qty};
    return true;
}

bool MarketSimulator::fill_trade(GeneratedMessage& out, SymbolId sym, SymbolState& s) {
    if (s.live_ids.empty()) return false;
    OrderId id = random_live(s);
    LiveOrder& o = s.orders.at(id);
    std::uniform_int_distribution<Quantity> d(1, o.qty);
    Quantity fill = d(rng_);

    out.type = MsgType::Trade;
    out.trade = Trade{sym, id, o.price, fill, opposite(o.side)};

    if (fill >= o.qty) {
        unregister_order(s, id);
    } else {
        remove_depth(s, o.side, o.price, fill);
        o.qty -= fill;
    }
    return true;
}

BookSnapshot MarketSimulator::make_snapshot(SymbolId sym, const SymbolState& s) const {
    BookSnapshot snap;
    snap.symbol = sym;
    std::size_t i = 0;
    for (auto it = s.bid_qty.rbegin(); it != s.bid_qty.rend() && i < kMaxSnapshotLevels; ++it, ++i)
        snap.bids[i] = {it->first, it->second};
    snap.num_bid_levels = static_cast<std::uint8_t>(i);
    i = 0;
    for (auto it = s.ask_qty.begin(); it != s.ask_qty.end() && i < kMaxSnapshotLevels; ++it, ++i)
        snap.asks[i] = {it->first, it->second};
    snap.num_ask_levels = static_cast<std::uint8_t>(i);
    return snap;
}

bool MarketSimulator::next(GeneratedMessage& out) {
    if (produced_ >= cfg_.num_messages) return false;

    out = GeneratedMessage{};
    out.seq = next_seq_++;
    out.ts = ts_;
    ts_ += cfg_.ts_step_ns;

    std::uniform_int_distribution<std::uint32_t> sym_dist(0, cfg_.num_symbols - 1);
    SymbolId sym = sym_dist(rng_);
    SymbolState& s = symbols_[sym];

    const std::uint64_t idx = produced_ + 1;  // 1-based for interval checks

    if (cfg_.heartbeat_interval && idx % cfg_.heartbeat_interval == 0) {
        out.type = MsgType::Heartbeat;
    } else if (cfg_.snapshot_interval && idx % cfg_.snapshot_interval == 0) {
        out.type = MsgType::BookSnapshot;
        out.snap = make_snapshot(sym, s);
    } else {
        std::uniform_int_distribution<int> mix(0, 99);
        int r = mix(rng_);
        bool done = false;
        if (r < cfg_.w_add) {
            fill_add(out, sym, s);
            done = true;
        } else if (r < cfg_.w_add + cfg_.w_cancel) {
            done = fill_cancel(out, sym, s);
        } else if (r < cfg_.w_add + cfg_.w_cancel + cfg_.w_modify) {
            done = fill_modify(out, sym, s);
        } else {
            done = fill_trade(out, sym, s);
        }
        if (!done) fill_add(out, sym, s);  // fall back when book is empty
    }

    ++produced_;
    return true;
}

}  // namespace mdfh
