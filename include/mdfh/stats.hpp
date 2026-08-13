#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

// Latency accounting for the benchmark. Samples (in nanoseconds) are stored and
// percentiles are computed with std::nth_element at summary time -- exact, no
// bucketing error, at the cost of O(N) memory. For the benchmark sizes here
// (a few million samples) that is a fine tradeoff and keeps the numbers honest.
namespace mdfh {

struct LatencySummary {
    std::uint64_t count = 0;
    double mean_ns = 0;
    std::uint64_t min_ns = 0;
    std::uint64_t p50_ns = 0;
    std::uint64_t p95_ns = 0;
    std::uint64_t p99_ns = 0;
    std::uint64_t max_ns = 0;
};

class LatencyHistogram {
public:
    void reserve(std::size_t n) { samples_.reserve(n); }
    void add(std::uint64_t ns) { samples_.push_back(ns); sum_ += ns; }
    [[nodiscard]] std::size_t count() const noexcept { return samples_.size(); }

    [[nodiscard]] LatencySummary summarize() {
        LatencySummary s;
        s.count = samples_.size();
        if (samples_.empty()) return s;
        s.mean_ns = static_cast<double>(sum_) / static_cast<double>(s.count);
        s.min_ns = *std::min_element(samples_.begin(), samples_.end());
        s.max_ns = *std::max_element(samples_.begin(), samples_.end());
        s.p50_ns = percentile(0.50);
        s.p95_ns = percentile(0.95);
        s.p99_ns = percentile(0.99);
        return s;
    }

private:
    std::uint64_t percentile(double q) {
        if (samples_.empty()) return 0;
        std::size_t idx = static_cast<std::size_t>(
            q * static_cast<double>(samples_.size() - 1));
        std::nth_element(samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(idx),
                         samples_.end());
        return samples_[idx];
    }

    std::vector<std::uint64_t> samples_;
    std::uint64_t              sum_ = 0;
};

}  // namespace mdfh
