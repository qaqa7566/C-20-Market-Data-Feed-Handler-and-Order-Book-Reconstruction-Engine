#pragma once

#include <cstdint>
#include <unordered_map>

#include "mdfh/event_handler.hpp"
#include "mdfh/order_book.hpp"

// Owns one OrderBook per symbol and applies normalized events to the correct
// book. This is the business-logic sink -- it knows nothing about bytes,
// sockets, or sequence numbers (those live upstream in the parser/feed handler),
// which keeps the layering clean and each piece independently testable.
namespace mdfh {

class BookManager {
public:
    OrderBook& book(SymbolId symbol) {
        auto it = books_.find(symbol);
        if (it == books_.end()) it = books_.emplace(symbol, OrderBook{symbol}).first;
        return it->second;
    }

    [[nodiscard]] const OrderBook* find(SymbolId symbol) const {
        auto it = books_.find(symbol);
        return it == books_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::size_t symbol_count() const noexcept { return books_.size(); }

    [[nodiscard]] bool check_all_invariants() const {
        for (const auto& [sym, bk] : books_)
            if (!bk.check_invariants()) return false;
        return true;
    }

    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (const auto& [sym, bk] : books_) fn(sym, bk);
    }

    void clear() { books_.clear(); }

private:
    std::unordered_map<SymbolId, OrderBook> books_;
};

// Event counters, useful for the benchmark summary and sanity checks.
struct BookStats {
    std::uint64_t adds = 0, cancels = 0, modifies = 0, trades = 0;
    std::uint64_t snapshots = 0, heartbeats = 0, malformed = 0;
    std::uint64_t rejected = 0;  // events referencing an unknown order id
};

// A concrete HandlerLike that drives the books.
class BookBuilder {
public:
    BookBuilder() = default;

    void on_add_order(const MessageHeader&, const AddOrder& m);
    void on_cancel_order(const MessageHeader&, const CancelOrder& m);
    void on_modify_order(const MessageHeader&, const ModifyOrder& m);
    void on_trade(const MessageHeader&, const Trade& m);
    void on_snapshot(const MessageHeader&, const BookSnapshot& m);
    void on_heartbeat(const MessageHeader&);
    void on_malformed(std::size_t offset);

    [[nodiscard]] BookManager& manager() noexcept { return manager_; }
    [[nodiscard]] const BookManager& manager() const noexcept { return manager_; }
    [[nodiscard]] const BookStats& stats() const noexcept { return stats_; }

    // When true (default in debug), invariants are checked after every mutation.
    void set_verify(bool v) noexcept { verify_ = v; }

private:
    BookManager manager_;
    BookStats   stats_{};
    bool        verify_ =
#ifndef NDEBUG
        true;
#else
        false;
#endif
};

}  // namespace mdfh
