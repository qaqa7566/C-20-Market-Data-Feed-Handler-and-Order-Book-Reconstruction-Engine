#include "mdfh/capture.hpp"

#include "mdfh/byte_order.hpp"

namespace mdfh {

namespace {
void encode_capture_header(std::byte* p, std::uint64_t count) {
    for (std::size_t i = 0; i < kCaptureMagic.size(); ++i)
        p[i] = static_cast<std::byte>(kCaptureMagic[i]);
    std::byte* q = p + 8;
    q = detail::store_le<std::uint32_t>(q, kCaptureVersion);
    q = detail::store_le<std::uint32_t>(q, 0);  // reserved
    detail::store_le<std::uint64_t>(q, count);
}
}  // namespace

bool CaptureWriter::open(const std::string& path) {
    close();
    file_ = std::fopen(path.c_str(), "wb");
    if (!file_) return false;
    count_ = 0;
    std::array<std::byte, kCaptureHeaderSize> hdr{};
    encode_capture_header(hdr.data(), 0);
    return std::fwrite(hdr.data(), 1, hdr.size(), file_) == hdr.size();
}

bool CaptureWriter::write_raw(const std::byte* p, std::size_t n) {
    return std::fwrite(p, 1, n, file_) == n;
}

void CaptureWriter::close() {
    if (!file_) return;
    // Patch the message count into the header.
    std::array<std::byte, kCaptureHeaderSize> hdr{};
    encode_capture_header(hdr.data(), count_);
    std::fflush(file_);
    if (std::fseek(file_, 0, SEEK_SET) == 0)
        std::fwrite(hdr.data(), 1, hdr.size(), file_);
    std::fclose(file_);
    file_ = nullptr;
}

bool CaptureReader::open(const std::string& path) {
    valid_ = false;
    error_.clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error_ = "cannot open file"; return false; }

    std::array<std::byte, kCaptureHeaderSize> hdr{};
    if (std::fread(hdr.data(), 1, hdr.size(), f) != hdr.size()) {
        error_ = "file smaller than header";
        std::fclose(f);
        return false;
    }
    for (std::size_t i = 0; i < kCaptureMagic.size(); ++i) {
        if (hdr[i] != static_cast<std::byte>(kCaptureMagic[i])) {
            error_ = "bad magic";
            std::fclose(f);
            return false;
        }
    }
    count_ = detail::load_le<std::uint64_t>(hdr.data() + 16);

    // Read the remaining message region.
    if (std::fseek(f, 0, SEEK_END) != 0) { error_ = "seek failed"; std::fclose(f); return false; }
    long end = std::ftell(f);
    if (end < static_cast<long>(kCaptureHeaderSize)) { error_ = "truncated"; std::fclose(f); return false; }
    std::size_t body = static_cast<std::size_t>(end) - kCaptureHeaderSize;
    std::fseek(f, static_cast<long>(kCaptureHeaderSize), SEEK_SET);
    bytes_.resize(body);
    std::size_t got = std::fread(bytes_.data(), 1, body, f);
    std::fclose(f);
    if (got != body) { error_ = "short read"; return false; }
    valid_ = true;
    return true;
}

}  // namespace mdfh
