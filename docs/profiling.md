# Profiling & Sanitizing on Linux

All commands assume a build directory at `build/`.

## Sanitizer builds (development / CI)

AddressSanitizer + UndefinedBehaviorSanitizer catch memory errors and UB. The
full test suite runs clean under them.

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMDFH_ENABLE_ASAN_UBSAN=ON
cmake --build build-asan -j
cd build-asan && ctest --output-on-failure
```

ThreadSanitizer is the right tool for the concurrent pipeline and the SPSC
queue (ASan does not detect data races):

```bash
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
cmake --build build-tsan -j
./build-tsan/bin/mdfh_tests --gtest_filter='SPSCQueue.*:Pipeline.*'
```

## perf (CPU profiling)

Build an optimized binary with frame pointers so `perf` can unwind:

```bash
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer"
cmake --build build-rel -j

# Record and view the hottest functions.
perf record -g --call-graph fp ./build-rel/bin/mdfh_benchmark --messages 2000000
perf report --stdio | head -40

# Quick counter summary (IPC, cache misses, branch misses).
perf stat -d ./build-rel/bin/mdfh_benchmark --messages 2000000
```

Expect the order-book map operations (`std::map` node traversal / allocation)
and the `unordered_map` order locator to dominate — see the README's tradeoffs
section for the flat-array alternative.

## valgrind / callgrind

Cachegrind and callgrind are slow (10–50x) but give exact instruction counts and
a call graph independent of scheduling noise:

```bash
# Correctness / leak check (use a smaller message count; valgrind is slow).
valgrind --leak-check=full --error-exitcode=1 \
    ./build/bin/mdfh_replay --in data/feed.cap

# Call-graph profile; open callgrind.out.* in kcachegrind.
valgrind --tool=callgrind ./build-rel/bin/mdfh_benchmark --messages 200000
callgrind_annotate callgrind.out.*
```

## Reproducing the benchmark

Benchmarks are deterministic given a seed. The numbers in the README came from:

```bash
./build/bin/mdfh_benchmark --messages 2000000 --symbols 16 --seed 1 \
    --json data/bench_results.json
python3 tools/analyze_bench.py --markdown data/bench_results.json
```
