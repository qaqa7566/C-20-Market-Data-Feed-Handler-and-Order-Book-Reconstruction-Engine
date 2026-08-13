#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// Portable little-endian (de)serialization helpers.
//
// The wire protocol is defined as little-endian regardless of host byte order.
// We never reinterpret_cast a byte buffer into a struct: that would invoke
// undefined behaviour on unaligned access and would silently break on a
// big-endian host. Instead every scalar is copied byte-by-byte through these
// helpers, which the compiler lowers to a single MOV (plus BSWAP on a
// big-endian target). This keeps the code correct, alignment-safe, and fast.
namespace mdfh::detail {

template <typename T>
concept TriviallyCopyableScalar =
    std::is_trivially_copyable_v<T> &&
    (std::is_integral_v<T> || std::is_enum_v<T>);

// Underlying integer type of an enum, or the type itself otherwise. Written as
// a trait so std::underlying_type is never instantiated for a non-enum (which
// is a hard error).
template <typename T, bool = std::is_enum_v<T>>
struct underlying_integer { using type = T; };
template <typename T>
struct underlying_integer<T, true> { using type = std::underlying_type_t<T>; };
template <typename T>
using underlying_integer_t = typename underlying_integer<T>::type;

// Store `value` into `dst` (which must have room for sizeof(T) bytes) in
// little-endian order. Returns a pointer just past the written bytes.
template <TriviallyCopyableScalar T>
inline std::byte* store_le(std::byte* dst, T value) noexcept {
    using U = std::make_unsigned_t<underlying_integer_t<T>>;
    // Widen to uintmax_t so the `>> 8` at the end of the last iteration is
    // always a valid shift, even when sizeof(U) == 1.
    std::uintmax_t u = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(U); ++i) {
        dst[i] = static_cast<std::byte>(u & 0xFFU);
        u >>= 8;
    }
    return dst + sizeof(U);
}

// Load a little-endian scalar of type T from `src` (must have sizeof(T) bytes).
template <TriviallyCopyableScalar T>
[[nodiscard]] inline T load_le(const std::byte* src) noexcept {
    using U = std::make_unsigned_t<underlying_integer_t<T>>;
    U u = 0;
    for (std::size_t i = 0; i < sizeof(U); ++i) {
        const auto byte_val = static_cast<std::uintmax_t>(
            std::to_integer<std::uint8_t>(src[i]));
        u = static_cast<U>(u | (byte_val << (8U * i)));
    }
    return static_cast<T>(u);
}

}  // namespace mdfh::detail
