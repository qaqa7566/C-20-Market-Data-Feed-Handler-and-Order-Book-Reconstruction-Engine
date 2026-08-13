#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// Thin RAII wrappers over POSIX sockets. This file (and tcp/udp) is the only
// place that touches the sockets API; the rest of the system deals in byte
// spans, keeping networking cleanly separated from business logic.
namespace mdfh::net {

// Owns a socket file descriptor and closes it on destruction (move-only).
class Socket {
public:
    Socket() = default;
    explicit Socket(int fd) noexcept : fd_(fd) {}
    ~Socket() { close(); }

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    Socket& operator=(Socket&& o) noexcept {
        if (this != &o) { close(); fd_ = o.fd_; o.fd_ = -1; }
        return *this;
    }

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    void close() noexcept;
    int release() noexcept { int f = fd_; fd_ = -1; return f; }

private:
    int fd_ = -1;
};

// Send the entire buffer, looping over partial writes. Returns false on error
// or peer close. TCP-oriented.
[[nodiscard]] bool send_all(int fd, std::span<const std::byte> data);

// Receive up to buf.size() bytes. Returns bytes read (0 = peer closed,
// <0 = error).
[[nodiscard]] std::ptrdiff_t recv_some(int fd, std::span<std::byte> buf);

// Return the local port a socket is bound to (useful with ephemeral port 0).
[[nodiscard]] std::uint16_t local_port(int fd);

}  // namespace mdfh::net
