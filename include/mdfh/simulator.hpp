#pragma once

#include <cstdint>
#include <map>
#include <random>
#include <unordered_map>
#include <vector>

#include "mdfh/protocol.hpp"
#include "mdfh/types.hpp"

// Synthetic exchange feed generator.
//
// The simulator produces a deterministic (seeded) stream of protocol messages
// for multiple symbols. It maintains an internal model of its own resting
// orders so that CancelOrder / ModifyOrder / Trade messages always reference
// live order ids, and it only ever posts *non-crossing* passive liquidity;
// aggressive flow is modelled as explicit Trade prints against resting orders.
// It is therefore a liquidity + trade-print simulator, not a matching engine --
// stated plainly so nobody mistakes it for one. This keeps every reconstructed
// book uncrossed and lets invariant checks run in tests.
namespace mdfh {

struct SimConfig {
    std::uint32_t num_symbols     = 4;
    std::uint64_t num_messages    = 1'000'000;
    std::uint64_t seed            = 42;

    Price    start_price = 100 * kPriceScale;  // 100.0000 for symbol 0
    Price    symbol_price_step = 5 * kPriceScale;  // symbol i starts higher
    Price    tick        = kPriceScale / 100;  // 0.0100
    std::uint32_t band_levels = 10;            // how deep new orders may post
    Quantity min_qty     = 1;
    Quantity max_qty     = 500;

    TimestampNs ts_start   = 1'000'000'000;    // arbitrary epoch (ns)
    TimestampNs ts_step_ns = 1'000;            // 1 microsecond between messages

    // Action mix (integer weights summing to 100).
    int w_add = 50, w_cancel = 22, w_modify = 18, w_trade = 10;

    // 0 disables. Otherwise emit one every N messages.
    std::uint64_t snapshot_interval  = 0;
    std::uint64_t heartbeat_interval = 100'000;
};

struct GeneratedMessage {
    MsgType     type = MsgType::Heartbeat;
    Sequence    seq  = 0;
    TimestampNs ts   = 0;
    AddOrder     add{};
    CancelOrder  cancel{};
    ModifyOrder  modify{};
    Trade        trade{};
    BookSnapshot snap{};
};

class MarketSimulator {
public:
    explicit MarketSimulator(SimConfig cfg);

    // Produce the next message; false once num_messages have been generated.
    bool next(GeneratedMessage& out);

    // Drive `sink` with every generated message. `sink` must be callable as
    // sink(Sequence, TimestampNs, const Body&) for each concrete body type
    // (a generic lambda works). Returns the number of messages generated.
    template <typename Sink>
    std::uint64_t run(Sink&& sink) {
        GeneratedMessage m;
        std::uint64_t n = 0;
        while (next(m)) { dispatch(m, sink); ++n; }
        return n;
    }

    [[nodiscard]] const SimConfig& config() const noexcept { return cfg_; }

private:
    struct LiveOrder { Side side; Price price; Quantity qty; };

    struct SymbolState {
        Price ref_mid = 0;
        std::unordered_map<OrderId, LiveOrder> orders;
        std::vector<OrderId> live_ids;                  // for O(1) random pick
        std::unordered_map<OrderId, std::size_t> index; // id -> live_ids slot
        std::map<Price, Quantity> bid_qty;              // aggregate depth
        std::map<Price, Quantity> ask_qty;
    };

    template <typename Sink>
    static void dispatch(const GeneratedMessage& m, Sink& sink) {
        switch (m.type) {
            case MsgType::AddOrder:     sink(m.seq, m.ts, m.add);    break;
            case MsgType::CancelOrder:  sink(m.seq, m.ts, m.cancel); break;
            case MsgType::ModifyOrder:  sink(m.seq, m.ts, m.modify); break;
            case MsgType::Trade:        sink(m.seq, m.ts, m.trade);  break;
            case MsgType::BookSnapshot: sink(m.seq, m.ts, m.snap);   break;
            case MsgType::Heartbeat:    sink(m.seq, m.ts, Heartbeat{}); break;
        }
    }

    // Model helpers.
    void add_depth(SymbolState& s, Side side, Price p, Quantity q);
    void remove_depth(SymbolState& s, Side side, Price p, Quantity q);
    void register_order(SymbolState& s, OrderId id, Side side, Price p, Quantity q);
    void unregister_order(SymbolState& s, OrderId id);
    OrderId random_live(SymbolState& s);
    Price choose_price(const SymbolState& s, Side side);
    Quantity random_qty();
    BookSnapshot make_snapshot(SymbolId sym, const SymbolState& s) const;

    void fill_add(GeneratedMessage& out, SymbolId sym, SymbolState& s);
    bool fill_cancel(GeneratedMessage& out, SymbolId sym, SymbolState& s);
    bool fill_modify(GeneratedMessage& out, SymbolId sym, SymbolState& s);
    bool fill_trade(GeneratedMessage& out, SymbolId sym, SymbolState& s);

    SimConfig                 cfg_;
    std::mt19937_64           rng_;
    std::vector<SymbolState>  symbols_;
    std::uint64_t             produced_ = 0;
    Sequence                  next_seq_ = 1;
    OrderId                   next_order_id_ = 1;
    TimestampNs               ts_ = 0;
};

}  // namespace mdfh
