#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "mdfh/net/socket.hpp"

// TCP transport. TCP is a byte stream, so message boundaries are recovered by
// the parser's self-describing framing (header carries the body size); the
// FeedHandler's residual buffer stitches messages split across segments.
namespace mdfh::net {

class TcpServer {
public:
    // Bind and listen. Pass port 0 to get an ephemeral port (see port()).
    [[nodiscard]] bool listen(std::uint16_t port, int backlog = 4);
    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

    // Block until a client connects; returns the connection socket.
    [[nodiscard]] std::optional<Socket> accept();

    [[nodiscard]] bool valid() const noexcept { return listen_.valid(); }

private:
    Socket        listen_;
    std::uint16_t port_ = 0;
};

class TcpClient {
public:
    [[nodiscard]] bool connect(const std::string& host, std::uint16_t port);
    [[nodiscard]] Socket& socket() noexcept { return sock_; }
    [[nodiscard]] int fd() const noexcept { return sock_.fd(); }

private:
    Socket sock_;
};

}  // namespace mdfh::net
