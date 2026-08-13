#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include <thread>

#include "mdfh/book_builder.hpp"
#include "mdfh/feed_handler.hpp"
#include "mdfh/protocol.hpp"
#include "mdfh/replay.hpp"  // book_fingerprint
#include "mdfh/serialization.hpp"
#include "mdfh/spsc_queue.hpp"

// Concurrent feed pipeline:
//
//   ingestion thread  --(SPSC frames)-->  book-update thread  --(SPSC ticks)-->
//   analytics thread
//
// Ownership & synchronization (documented, deliberately minimal):
//   * Each queue is strictly single-producer / single-consumer, which is what
//     lets SPSCQueue be lock-free. Frame ownership transfers by value through
//     the ring; once pushed, the producer never touches that slot again.
//   * The order book is owned exclusively by the book-update thread. No lock
//     guards it because no other thread reads or writes it while running.
//   * Analytics receives *copies* of top-of-book state, so it never races the
//     book.
//   * Shutdown uses two atomic "done" flags with release/acquire ordering.
//
// Because the book-update thread consumes frames in the exact FIFO order the
// ingestion thread produced them, the final book state is identical to the
// single-threaded path -- verified in tests via book_fingerprint().
namespace mdfh {

struct Frame {
    std::uint16_t len = 0;
    std::array<std::byte, kMaxMessageSize> data{};
};

struct Tick {
    SymbolId sym = 0;
    Price    bid = 0;
    Price    ask = 0;
};

struct PipelineResult {
    std::uint64_t messages_parsed  = 0;
    std::uint64_t malformed        = 0;
    std::uint64_t analytics_events = 0;
    double        wall_seconds     = 0.0;
};

class ConcurrentPipeline {
public:
    struct Config {
        std::size_t   frame_queue_capacity = 1u << 16;
        std::size_t   tick_queue_capacity  = 1u << 16;
        std::uint64_t analytics_every      = 512;  // sample every N messages
    };

    ConcurrentPipeline() = default;
    explicit ConcurrentPipeline(Config cfg) : cfg_(cfg) {}

    // Process an in-memory capture through the 3-thread pipeline.
    PipelineResult run(std::span<const std::byte> messages) {
        SPSCQueue<Frame> frames(cfg_.frame_queue_capacity);
        SPSCQueue<Tick>  ticks(cfg_.tick_queue_capacity);
        std::atomic<bool> ingest_done{false};
        std::atomic<bool> book_done{false};
        PipelineResult result;

        const auto t0 = std::chrono::steady_clock::now();

        // --- Stage 1: ingestion (frame the byte stream) ---
        std::thread ingest([&] {
            std::size_t off = 0;
            while (off + kHeaderWireSize <= messages.size()) {
                MessageHeader hdr;
                if (!decode_header(messages.subspan(off, kHeaderWireSize), hdr)) break;
                std::size_t frame_len = kHeaderWireSize + hdr.body_size;
                if (off + frame_len > messages.size()) break;
                if (frame_len > kMaxMessageSize) { off += frame_len; continue; }

                Frame f;
                f.len = static_cast<std::uint16_t>(frame_len);
                std::copy_n(messages.data() + off, frame_len, f.data.data());
                while (!frames.try_push(f)) std::this_thread::yield();  // backpressure
                off += frame_len;
            }
            ingest_done.store(true, std::memory_order_release);
        });

        // --- Stage 2: parse + book update, emit periodic ticks ---
        std::thread book([&] {
            BookBuilder builder;
            builder.set_verify(false);  // hot path; invariants covered by tests
            FeedHandler<BookBuilder> feed(builder);
            std::uint64_t processed = 0;
            Frame f;
            for (;;) {
                if (frames.try_pop(f)) {
                    ParseResult r = feed.consume_datagram({f.data.data(), f.len});
                    result.messages_parsed += r.messages;
                    result.malformed += r.malformed;
                    if (++processed % cfg_.analytics_every == 0) {
                        // Sample top-of-book of the last touched symbol cheaply:
                        // iterate is avoided; we sample symbol 0 for simplicity.
                        emit_tick(builder, ticks);
                    }
                } else if (ingest_done.load(std::memory_order_acquire) &&
                           frames.empty()) {
                    break;
                } else {
                    std::this_thread::yield();
                }
            }
            emit_tick(builder, ticks);
            fingerprint_ = book_fingerprint(builder.manager());
            final_symbol_count_ = builder.manager().symbol_count();
            book_stats_ = builder.stats();
            book_done.store(true, std::memory_order_release);
        });

        // --- Stage 3: analytics (drain ticks) ---
        std::thread analytics([&] {
            Tick t;
            for (;;) {
                if (ticks.try_pop(t)) {
                    ++result.analytics_events;
                    last_tick_ = t;
                } else if (book_done.load(std::memory_order_acquire) &&
                           ticks.empty()) {
                    break;
                } else {
                    std::this_thread::yield();
                }
            }
        });

        ingest.join();
        book.join();
        analytics.join();

        result.wall_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return result;
    }

    [[nodiscard]] std::size_t final_symbol_count() const noexcept { return final_symbol_count_; }
    [[nodiscard]] const BookStats& book_stats() const noexcept { return book_stats_; }
    [[nodiscard]] std::uint64_t fingerprint() const noexcept { return fingerprint_; }

private:
    static void emit_tick(const BookBuilder& b, SPSCQueue<Tick>& ticks) {
        b.manager().for_each([&](SymbolId sym, const OrderBook& bk) {
            Tick t;
            t.sym = sym;
            if (auto bb = bk.best_bid()) t.bid = bb->price;
            if (auto ba = bk.best_ask()) t.ask = ba->price;
            (void)ticks.try_push(t);  // drop on overflow -- analytics is lossy
        });
    }

    Config       cfg_{};
    std::size_t  final_symbol_count_ = 0;
    BookStats    book_stats_{};
    std::uint64_t fingerprint_ = 0;
    Tick         last_tick_{};
};

}  // namespace mdfh
