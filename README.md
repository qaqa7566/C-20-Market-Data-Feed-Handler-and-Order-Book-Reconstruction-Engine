# Market Data Feed Handler & Order Book Reconstruction Engine

A production-style **C++20** system that simulates an exchange market-data feed
and reconstructs the exchange limit order book from incremental messages. It is
built to demonstrate systems programming, market-data processing, correctness
under adversarial inputs, testing discipline, and honest performance
engineering — not to be a trading bot or a UI.

```
 exchange simulator ─► binary protocol ─► transport (file / TCP / UDP)
        │                                          │
        │                                          ▼
        │                              feed handler (framing + sequence
        │                              validation: dup / gap / out-of-order)
        │                                          │
        │                                          ▼
        │                              binary parser (zero-copy, no alloc)
        │                                          │
        │                                          ▼
        │                              normalized events (callback dispatch)
        │                                          │
        │                                          ▼
        └──── deterministic replay ───►  per-symbol order-book reconstruction
                                                   │
                                                   ▼
                                          analytics / snapshots
```

- 45 GoogleTest cases, green under **AddressSanitizer + UndefinedBehaviorSanitizer**.
- Clean `-Wall -Wextra -Wpedantic -Wconversion` build; `-Werror` verified on **GCC 13** and **Clang 18**.
- Benchmark that processes **2,000,000** messages and reports real throughput and
  tail-latency percentiles (numbers below come from an actual run, not invented).

---

## Table of contents

1. [Motivation](#1-motivation)
2. [Architecture](#2-architecture)
3. [Market-data protocol](#3-market-data-protocol)
4. [Order-book design](#4-order-book-design)
5. [Concurrency model](#5-concurrency-model)
6. [Complexity analysis](#6-complexity-analysis)
7. [Build instructions](#7-build-instructions)
8. [Usage examples](#8-usage-examples)
9. [Testing](#9-testing)
10. [Benchmarking methodology](#10-benchmarking-methodology)
11. [Measured results](#11-measured-results)
12. [Linux profiling](#12-linux-profiling)
13. [Design tradeoffs](#13-design-tradeoffs)
14. [Limitations & future work](#14-limitations--future-work)

---

## 1. Motivation

Reconstructing an order book from an incremental feed is the canonical
market-data engineering problem: it exercises binary protocol design, robust
parsing of untrusted bytes, sequence-gap detection, careful data-structure
choice for the book, and measurable latency. This project implements that
pipeline end to end with an emphasis on **correctness first, then measured
performance**, and it documents its tradeoffs honestly so the code can be
discussed in depth in an interview setting.

## 2. Architecture

The system is layered so that each stage has a single responsibility and can be
tested in isolation. Bytes flow one way; business logic never touches sockets.

| Layer | Component | Header |
|-------|-----------|--------|
| Transport | file capture, TCP, UDP | `net/tcp.hpp`, `net/udp.hpp`, `capture.hpp` |
| Framing + validation | `FeedHandler`, `SequenceTracker` | `feed_handler.hpp`, `sequence_tracker.hpp` |
| Parsing | `BinaryParser` (zero-copy) | `parser.hpp`, `serialization.hpp` |
| Normalized events | `EventHandler` / `HandlerLike` | `event_handler.hpp` |
| Reconstruction | `OrderBook`, `BookManager`, `BookBuilder` | `order_book.hpp`, `book_builder.hpp` |
| Concurrency | `SPSCQueue`, `ConcurrentPipeline` | `spsc_queue.hpp`, `pipeline.hpp` |
| Replay / analytics | `ReplayDriver`, `LatencyHistogram` | `replay.hpp`, `stats.hpp` |

The parser dispatches to a handler through a compile-time `HandlerLike`
concept, so the hot path has **no virtual calls and no per-message heap
allocation**. An abstract `EventHandler` base class is also provided for cases
where the sink must be chosen at runtime.

## 3. Market-data protocol

A compact, fixed-layout binary protocol — full byte-level spec in
[`docs/protocol.md`](docs/protocol.md). Highlights:

- **Little-endian** on the wire, decoded byte-by-byte through `store_le`/`load_le`
  helpers. Nothing is ever `reinterpret_cast` from a byte buffer into a struct,
  so there is no unaligned-access UB and the format is portable to a big-endian
  host.
- 16-byte header: `msg_type`, `version`, `body_size`, `sequence`, `timestamp_ns`.
- Message bodies: `AddOrder`, `CancelOrder`, `ModifyOrder`, `Trade`,
  `BookSnapshot`, `Heartbeat`.
- **Prices are scaled `int64` fixed point** (4 implied decimals) — exact
  comparison and ordering, deterministic replay, no float on the data path.
- No JSON anywhere on the market-data path.

## 4. Order-book design

One `OrderBook` per symbol with strict **price-time (FIFO) priority**:

- Bids: `std::map<Price, PriceLevel, std::greater<>>` → `begin()` is the best bid.
- Asks: `std::map<Price, PriceLevel, std::less<>>` → `begin()` is the best ask.
- Each `PriceLevel` holds a `std::list<RestingOrder>` in arrival order plus an
  aggregate quantity.
- A hash map `order_id → {side, price, list-iterator}` makes cancel/modify an
  O(1) locate followed by an O(1) splice — no scanning of the level.

`modify` mirrors real exchange semantics: a same-price size *reduction* keeps
time priority, while a price change or size *increase* re-queues the order at the
back of its level. `execute` applies partial and full fills. In debug/test
builds `check_invariants()` runs after every update, verifying level-quantity
totals, per-level ordering, the locator/order-count agreement, and that the book
is **never crossed** (best bid `<` best ask).

## 5. Concurrency model

The correct single-threaded path is the reference implementation. On top of it
sits an **optional** three-stage pipeline:

```
ingestion thread ─(SPSC frames)─► book-update thread ─(SPSC ticks)─► analytics thread
```

- Each queue is a bounded **lock-free single-producer/single-consumer ring**
  (`SPSCQueue`), correct by a release-store / acquire-load pair on the head and
  tail indices. It is lock-free *only* because it is strictly SPSC — that
  restriction is stated plainly, not dressed up as general lock-freedom. Head and
  tail sit on separate cache lines to avoid false sharing.
- The order book is owned exclusively by the book-update thread; **no mutex
  guards it** because nothing else reads or writes it. Analytics receives copies
  of top-of-book state, so it never races the book.
- Because the book-update thread consumes frames in the exact FIFO order they
  were produced, the concurrent path reconstructs a **bit-identical** final book
  to the single-threaded path. This is asserted in tests via a 64-bit
  order-independent `book_fingerprint`.

Shutdown uses two atomic `done` flags with release/acquire ordering. The threads
back off with `std::this_thread::yield()` rather than spinning hot.

## 6. Complexity analysis

Let `L` = number of distinct price levels on a side, `D` = requested depth.

| Operation | Cost | Notes |
|-----------|------|-------|
| `add` | O(log L) | map insert of a new level; O(1) when the level exists |
| `cancel` | O(log L) | O(1) locate via hash map; erase level if it empties |
| `modify` | O(log L) | reduction is O(1); re-queue is O(log L) |
| `execute` | O(log L) | O(1) locate + fill |
| `best_bid`/`best_ask`/`spread` | O(1) | `map::begin()` |
| depth query / snapshot | O(D) | walk from the top of book |
| parse one message | O(body size) = O(1) | fixed-size bodies, no allocation |
| sequence check | O(1) | |

Memory is O(active orders + active levels): one list node and one hash-map entry
per resting order, one map node per occupied price level.

## 7. Build instructions

Requirements: a C++20 compiler (GCC ≥ 11 / Clang ≥ 14) and CMake ≥ 3.20.
GoogleTest is fetched automatically via `FetchContent`.

```bash
# Release build (library, apps, benchmark, tests)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Debug build with sanitizers
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMDFH_ENABLE_ASAN_UBSAN=ON
cmake --build build-asan -j
```

Options: `MDFH_BUILD_TESTS`, `MDFH_BUILD_BENCHMARKS`, `MDFH_BUILD_APPS`,
`MDFH_ENABLE_ASAN_UBSAN`, `MDFH_WARNINGS_AS_ERRORS` (all default to sensible
values; see `CMakeLists.txt`).

## 8. Usage examples

```bash
# Generate a 1M-message capture across 8 symbols (deterministic given the seed).
./build/bin/mdfh_feedsim --messages 1000000 --symbols 8 --seed 42 --out data/feed.cap

# Reconstruct the book from the capture and print top-of-book per symbol.
./build/bin/mdfh_replay --in data/feed.cap --mode max

# Accelerated (timestamp-paced) replay at 50x.
./build/bin/mdfh_replay --in data/feed.cap --mode accel --speed 50

# Live feed over TCP: start the publisher, then the handler.
./build/bin/mdfh_feedsim --messages 500000 --tcp 9001 &
./build/bin/mdfh_feedhandler --tcp 127.0.0.1:9001

# Live feed over UDP (lossy transport; the handler reports dup/gap/ooo stats).
./build/bin/mdfh_feedsim --messages 500000 --udp 127.0.0.1:9002 &
./build/bin/mdfh_feedhandler --udp 9002
```

## 9. Testing

GoogleTest suite (`tests/`), run with `ctest`:

```bash
cd build && ctest --output-on-failure
```

45 cases cover binary serialization round-trips and little-endianness; malformed,
duplicate, gapped, and out-of-order packets; partial-tail stream reassembly;
add/cancel/modify/trade and partial executions; multi-symbol books; empty and
crossed-book invariants; FIFO priority; deterministic replay across speeds;
capture read/write and corrupt-file rejection; the lock-free SPSC queue under a
1M-item concurrent transfer; concurrency correctness (pipeline vs single-thread
fingerprint); and TCP/UDP loopback reconstruction. The whole suite passes under
ASan + UBSan.

## 10. Benchmarking methodology

`mdfh_benchmark` generates the feed into memory (untimed), then measures:

1. **Single-threaded throughput** — one bulk `consume()` over the whole buffer,
   timed with `std::chrono::steady_clock`.
2. **Per-message latency distribution** — a separate pass timing each message's
   parse-and-apply individually, summarized to avg/p50/p95/p99 with
   `std::nth_element` (exact percentiles, no bucketing error). Each sample
   includes two `steady_clock::now()` calls (~tens of ns of measurement
   overhead), so the reported per-message latency is a slight **over**-estimate —
   deliberately, so the numbers are not flattering.
3. **Concurrent pipeline throughput** — the same buffer through the three-thread
   pipeline, plus a determinism check against the single-threaded fingerprint.

Nothing is hard-coded; re-run it yourself with the commands below. Results are
deterministic given `--seed`.

## 11. Measured results

Environment: Intel Xeon @ 2.80 GHz, **2 vCPU** cloud VM, GCC 13.3, `-O3`.
Command:

```bash
./build/bin/mdfh_benchmark --messages 2000000 --symbols 16 --seed 1 \
    --json data/bench_results.json
```

| Metric | Single-threaded | Concurrent pipeline (3 threads) |
|--------|----------------:|--------------------------------:|
| Messages processed | 2,000,000 | 2,000,000 |
| Wall time | 1.188 s | 1.708 s |
| Throughput | **1.68 M msg/s** | 1.17 M msg/s |
| Avg latency | 565 ns | — |
| p50 / p95 / p99 latency | 500 / 987 / 1241 ns | — |
| min / max latency | 49 ns / 3.20 ms | — |
| Malformed / dropped | 0 / 0 | 0 / 0 |
| Deterministic vs single-thread | — | **yes** |

Two honest observations worth discussing:

- **The concurrent pipeline is *slower* here (0.70×), not faster.** On a 2-vCPU
  machine, three busy threads oversubscribe the cores, and every message pays an
  extra frame copy into the ring plus queue synchronization — while the
  book-update stage (the real cost) still runs on a single core. The pipeline
  wins when the transport is the bottleneck and parsing/book-building can occupy
  a dedicated core on a machine with cores to spare; it loses when the workload
  is CPU-bound on too few cores. The value delivered here is the *architecture
  and the proven-identical output*, not a speedup on this box.
- **The `max` latency (3.2 ms) is a cold-start tail**, dominated by first-touch
  page faults and `std::map` node allocations / rehashes early in the run, not
  steady-state behaviour — visible in the gap between p99 (1.24 µs) and max.

Raw JSON: [`data/bench_results.json`](data/bench_results.json). Summarize or
compare runs with `python3 tools/analyze_bench.py --markdown data/bench_results.json`.

This is a single-machine, single-feed measurement on a shared cloud VM. It is
deliberately **not** described as "ultra-low-latency": the numbers do not support
that claim, and they are reported as measured.

## 12. Linux profiling

Full commands in [`docs/profiling.md`](docs/profiling.md): `perf record` /
`perf stat` for CPU hotspots, `valgrind --tool=callgrind` for exact
instruction-level call graphs, and ThreadSanitizer for the concurrent path
(ASan does not detect data races). The expected hotspots are the `std::map`
traversal/allocation and the `unordered_map` order locator — which motivates the
flat-array alternative discussed next.

## 13. Design tradeoffs

- **`std::map` book vs flat array.** The map gives obviously-correct O(log L)
  operations and O(1) best-price, at the cost of pointer chasing and a node
  allocation per new level. For a bounded tick range, an array indexed by
  `(price − floor)/tick` gives O(1) everything and far better cache behaviour.
  Correctness-first: the map is the reference; the array is the documented next
  optimization.
- **Compile-time (`HandlerLike`) vs virtual dispatch.** The hot path is templated
  for zero-overhead dispatch; a virtual `EventHandler` is available where runtime
  polymorphism is needed.
- **Forward-only sequence handling.** The `SequenceTracker` *classifies*
  duplicates/gaps/out-of-order without reordering or buffering. Recovery
  (retransmission requests, snapshot fail-over) is a policy that belongs in a
  higher layer; keeping the tracker side-effect-free makes it trivially testable.
- **Snapshots are analytics, not book initializers.** Reconstructing exact FIFO
  state from an aggregated snapshot is impossible, so the incremental book is
  authoritative. A production handler would use a snapshot to *initialize* a book
  and then apply increments with `seq >` the snapshot's.
- **Simulator models liquidity + trade prints, not matching.** It posts only
  non-crossing passive orders and emits explicit `Trade`s against resting orders,
  which keeps every reconstructed book uncrossed. It is not a matching engine and
  does not pretend to be.
- **Lock-free where it is genuinely lock-free.** The SPSC ring is lock-free; the
  pipeline's shutdown/backoff uses atomics and `yield`. Nothing is labelled
  lock-free that is not.

## 14. Limitations & future work

- No matching engine; the simulator does not cross the book.
- No gap recovery / retransmission or snapshot-driven book initialization.
- TCP publisher serves a single client; UDP assumes one message per datagram
  within the MTU and no reassembly.
- Flat-array order book keyed by tick for O(1) hot-path and cache locality.
- Thread affinity / NUMA pinning, and a `perf`-guided (PGO) build.
- A larger, multi-machine benchmark to characterize the pipeline where it is
  expected to help (transport-bound, cores to spare).

---

*Built as a portfolio project. Feedback via the repo's issues is welcome.*
