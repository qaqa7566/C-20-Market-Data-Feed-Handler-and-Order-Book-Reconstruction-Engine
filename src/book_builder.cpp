#include "mdfh/book_builder.hpp"

#include <cassert>

namespace mdfh {

void BookBuilder::on_add_order(const MessageHeader&, const AddOrder& m) {
    ++stats_.adds;
    OrderBook& bk = manager_.book(m.symbol);
    if (!bk.add(m.order_id, m.side, m.price, m.quantity)) ++stats_.rejected;
    if (verify_) assert(bk.check_invariants());
}

void BookBuilder::on_cancel_order(const MessageHeader&, const CancelOrder& m) {
    ++stats_.cancels;
    OrderBook& bk = manager_.book(m.symbol);
    if (!bk.cancel(m.order_id)) ++stats_.rejected;
    if (verify_) assert(bk.check_invariants());
}

void BookBuilder::on_modify_order(const MessageHeader&, const ModifyOrder& m) {
    ++stats_.modifies;
    OrderBook& bk = manager_.book(m.symbol);
    if (!bk.modify(m.order_id, m.new_price, m.new_quantity)) ++stats_.rejected;
    if (verify_) assert(bk.check_invariants());
}

void BookBuilder::on_trade(const MessageHeader&, const Trade& m) {
    ++stats_.trades;
    OrderBook& bk = manager_.book(m.symbol);
    // A trade consumes liquidity from the resting (passive) order.
    if (!bk.execute(m.resting_order_id, m.quantity)) ++stats_.rejected;
    if (verify_) assert(bk.check_invariants());
}

void BookBuilder::on_snapshot(const MessageHeader&, const BookSnapshot&) {
    // Snapshots in this system are periodic analytics emitted by the exchange.
    // Reconstructing exact per-order FIFO state from an aggregated snapshot is
    // not possible, so the incremental book is authoritative and snapshots are
    // counted but not applied. A production handler would instead use a
    // snapshot to *initialize* a book and then apply increments with seq >
    // snapshot seq.
    ++stats_.snapshots;
}

void BookBuilder::on_heartbeat(const MessageHeader&) { ++stats_.heartbeats; }

void BookBuilder::on_malformed(std::size_t) { ++stats_.malformed; }

}  // namespace mdfh
