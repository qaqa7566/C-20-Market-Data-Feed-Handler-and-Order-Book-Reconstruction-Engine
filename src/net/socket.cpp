#include "mdfh/net/socket.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

namespace mdfh::net {

void Socket::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool send_all(int fd, std::span<const std::byte> data) {
    const std::byte* p = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        ssize_t n = ::send(fd, p, remaining, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        p += n;
        remaining -= static_cast<std::size_t>(n);
    }
    return true;
}

std::ptrdiff_t recv_some(int fd, std::span<std::byte> buf) {
    for (;;) {
        ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
        if (n < 0 && errno == EINTR) continue;
        return n;
    }
}

std::uint16_t local_port(int fd) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return 0;
    return ntohs(addr.sin_port);
}

}  // namespace mdfh::net
