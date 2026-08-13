#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Core value types shared across the market-data feed handler.
//
// Design notes:
//  * Prices are represented as scaled 64-bit integers ("fixed point"), never
//    floating point on the wire or inside the book. A raw Price of 1'234'500
//    with kPriceScale == 10'000 means 123.45. Integer prices give exact
//    equality, exact ordering, and deterministic replay -- all properties a
//    floating point representation would compromise.
//  * SymbolId is a small integer handle (compare ITCH's "stock locate" code).
//    The mapping between human-readable tickers and ids lives in a SymbolTable
//    so the hot path never touches strings.
namespace mdfh {

using Price      = std::int64_t;   // scaled fixed-point price (see kPriceScale)
using Quantity   = std::uint32_t;  // number of shares/contracts
using OrderId    = std::uint64_t;  // exchange-assigned unique order identifier
using SymbolId   = std::uint32_t;  // compact symbol handle
using Sequence   = std::uint32_t;  // per-feed monotonically increasing seq no.
using TimestampNs = std::uint64_t; // nanoseconds since an arbitrary epoch

// Number of price sub-units per whole unit (4 implied decimal places).
inline constexpr std::int64_t kPriceScale = 10'000;

enum class Side : std::uint8_t {
    Buy  = 0,
    Sell = 1,
};

[[nodiscard]] constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

[[nodiscard]] constexpr std::string_view to_string(Side s) noexcept {
    return s == Side::Buy ? "BUY" : "SELL";
}

// Convert a scaled integer price to a human-readable double. For display and
// diagnostics only -- never used for comparison or storage.
[[nodiscard]] inline double price_to_double(Price p) noexcept {
    return static_cast<double>(p) / static_cast<double>(kPriceScale);
}

[[nodiscard]] inline Price double_to_price(double d) noexcept {
    // Round to nearest tick.
    return static_cast<Price>(d * static_cast<double>(kPriceScale) +
                              (d >= 0 ? 0.5 : -0.5));
}

}  // namespace mdfh
