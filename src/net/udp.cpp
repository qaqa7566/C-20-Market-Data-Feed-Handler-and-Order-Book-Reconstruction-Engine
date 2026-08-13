#include "mdfh/net/udp.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>

namespace mdfh::net {

bool UdpSender::open(const std::string& host, std::uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    Socket s(fd);

    in_addr a{};
    if (::inet_pton(AF_INET, host.c_str(), &a) != 1) return false;
    dst_addr_ = a.s_addr;
    dst_port_ = htons(port);
    sock_ = std::move(s);
    return true;
}

bool UdpSender::send(std::span<const std::byte> data) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = dst_addr_;
    addr.sin_port = dst_port_;
    ssize_t n = ::sendto(sock_.fd(), data.data(), data.size(), 0,
                         reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return n == static_cast<ssize_t>(data.size());
}

bool UdpReceiver::bind(std::uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    Socket s(fd);

    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;

    port_ = local_port(fd);
    sock_ = std::move(s);
    return true;
}

std::ptrdiff_t UdpReceiver::recv(std::span<std::byte> buf) {
    return ::recvfrom(sock_.fd(), buf.data(), buf.size(), 0, nullptr, nullptr);
}

bool UdpReceiver::set_recv_timeout_ms(int ms) {
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return ::setsockopt(sock_.fd(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

}  // namespace mdfh::net
