#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include "mdfh/protocol.hpp"
#include "mdfh/serialization.hpp"

// Binary capture file: a fixed 24-byte header followed by the exact bytes that
// would have gone on the wire (framed messages, back to back). Replaying a
// capture therefore exercises the identical parse/book path as a live feed.
//
//   Header: magic[8]="MDFHCAP1", u32 version, u32 reserved, u64 message_count
namespace mdfh {

inline constexpr std::array<char, 8> kCaptureMagic = {'M','D','F','H','C','A','P','1'};
inline constexpr std::uint32_t kCaptureVersion = 1;
inline constexpr std::size_t kCaptureHeaderSize = 24;

// Streams framed messages to disk with bounded memory. The message count in the
// header is patched on close().
class CaptureWriter {
public:
    CaptureWriter() = default;
    ~CaptureWriter() { close(); }
    CaptureWriter(const CaptureWriter&) = delete;
    CaptureWriter& operator=(const CaptureWriter&) = delete;

    [[nodiscard]] bool open(const std::string& path);
    void close();
    [[nodiscard]] bool is_open() const noexcept { return file_ != nullptr; }
    [[nodiscard]] std::uint64_t message_count() const noexcept { return count_; }

    // Encode and append one framed message.
    template <typename Body>
    bool write(Sequence seq, TimestampNs ts, const Body& body) {
        std::array<std::byte, kMaxMessageSize> buf{};
        std::size_t n = encode_message(buf, seq, ts, body);
        if (n == 0) return false;
        if (!write_raw(buf.data(), n)) return false;
        ++count_;
        return true;
    }

private:
    bool write_raw(const std::byte* p, std::size_t n);

    std::FILE*    file_ = nullptr;
    std::uint64_t count_ = 0;
};

// Loads a capture fully into memory and exposes the message region as a span.
class CaptureReader {
public:
    [[nodiscard]] bool open(const std::string& path);

    [[nodiscard]] std::span<const std::byte> messages() const {
        return {bytes_.data(), bytes_.size()};
    }
    [[nodiscard]] std::uint64_t message_count() const noexcept { return count_; }
    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    std::vector<std::byte> bytes_;      // message region only (header stripped)
    std::uint64_t          count_ = 0;
    bool                   valid_ = false;
    std::string            error_;
};

}  // namespace mdfh
