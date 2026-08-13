#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "mdfh/net/socket.hpp"

// UDP transport. Each datagram carries one or more whole framed messages (the
// sender never splits a message across datagrams). UDP can drop, duplicate, and
// reorder packets -- exactly the conditions the SequenceTracker is built to
// detect. A datagram MTU-bounded payload is assumed.
namespace mdfh::net {

class UdpSender {
public:
    [[nodiscard]] bool open(const std::string& host, std::uint16_t port);
    // Send one datagram. Returns false on error.
    [[nodiscard]] bool send(std::span<const std::byte> data);

private:
    Socket        sock_;
    // Destination sockaddr stored opaquely to keep <netinet/in.h> out of headers.
    std::uint32_t dst_addr_ = 0;  // network byte order
    std::uint16_t dst_port_ = 0;  // network byte order
};

class UdpReceiver {
public:
    // Bind to a port (0 => ephemeral, see port()).
    [[nodiscard]] bool bind(std::uint16_t port);
    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

    // Receive one datagram into buf; returns bytes read (<0 on error).
    [[nodiscard]] std::ptrdiff_t recv(std::span<std::byte> buf);

    // Set a receive timeout so a stuck receiver cannot hang a test forever.
    [[nodiscard]] bool set_recv_timeout_ms(int ms);

    [[nodiscard]] int fd() const noexcept { return sock_.fd(); }

private:
    Socket        sock_;
    std::uint16_t port_ = 0;
};

}  // namespace mdfh::net
