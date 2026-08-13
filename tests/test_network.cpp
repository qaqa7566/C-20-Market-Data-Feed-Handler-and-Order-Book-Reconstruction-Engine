#include <gtest/gtest.h>

#include <array>
#include <thread>
#include <vector>

#include "mdfh/book_builder.hpp"
#include "mdfh/feed_handler.hpp"
#include "mdfh/net/tcp.hpp"
#include "mdfh/net/udp.hpp"
#include "mdfh/replay.hpp"
#include "test_util.hpp"

using namespace mdfh;

namespace {
std::uint64_t reference_fingerprint(std::span<const std::byte> feed) {
    BookBuilder b;
    b.set_verify(false);
    FeedHandler<BookBuilder> fh(b);
    fh.consume(feed);
    return book_fingerprint(b.manager());
}
}  // namespace

TEST(Network, TcpLoopbackReconstructsIdenticalBook) {
    auto feed = test::build_sim_feed(50'000, 4, 314);
    std::uint64_t ref = reference_fingerprint(feed);

    net::TcpServer server;
    ASSERT_TRUE(server.listen(0));
    std::uint16_t port = server.port();

    std::thread server_thread([&] {
        auto conn = server.accept();
        ASSERT_TRUE(conn.has_value());
        ASSERT_TRUE(net::send_all(conn->fd(), feed));
        // conn closes on scope exit -> client sees EOF.
    });

    net::TcpClient client;
    ASSERT_TRUE(client.connect("127.0.0.1", port));

    BookBuilder builder;
    builder.set_verify(false);
    FeedHandler<BookBuilder> fh(builder);
    std::vector<std::byte> buf(16 * 1024);
    for (;;) {
        std::ptrdiff_t n = net::recv_some(client.fd(), buf);
        if (n <= 0) break;
        fh.consume({buf.data(), static_cast<std::size_t>(n)});
    }
    server_thread.join();

    EXPECT_EQ(fh.malformed(), 0u);
    EXPECT_TRUE(builder.manager().check_all_invariants());
    EXPECT_EQ(book_fingerprint(builder.manager()), ref);
}

TEST(Network, UdpLoopbackDeliversDatagrams) {
    // Small volume so the kernel receive buffer holds every datagram on
    // loopback (each message is one datagram of a few dozen bytes).
    auto feed = test::build_sim_feed(1'000, 3, 271);
    std::uint64_t ref = reference_fingerprint(feed);

    net::UdpReceiver rx;
    ASSERT_TRUE(rx.bind(0));
    ASSERT_TRUE(rx.set_recv_timeout_ms(500));
    std::uint16_t port = rx.port();

    net::UdpSender tx;
    ASSERT_TRUE(tx.open("127.0.0.1", port));

    // Send each framed message as its own datagram.
    std::size_t off = 0;
    std::size_t sent = 0;
    while (off + kHeaderWireSize <= feed.size()) {
        MessageHeader hdr;
        ASSERT_TRUE(decode_header({feed.data() + off, kHeaderWireSize}, hdr));
        std::size_t frame = kHeaderWireSize + hdr.body_size;
        ASSERT_TRUE(tx.send({feed.data() + off, frame}));
        off += frame;
        ++sent;
    }

    BookBuilder builder;
    builder.set_verify(false);
    FeedHandler<BookBuilder> fh(builder);
    std::array<std::byte, 2048> buf{};
    std::size_t received = 0;
    for (;;) {
        std::ptrdiff_t n = rx.recv(buf);
        if (n <= 0) break;  // idle timeout
        fh.consume_datagram({buf.data(), static_cast<std::size_t>(n)});
        ++received;
    }

    EXPECT_EQ(fh.malformed(), 0u);
    EXPECT_TRUE(builder.manager().check_all_invariants());
    EXPECT_GT(received, 0u);
    // On loopback at this small volume we expect no loss; if that holds, the
    // reconstructed book must match the reference exactly.
    if (received == sent) {
        EXPECT_EQ(book_fingerprint(builder.manager()), ref);
    }
}
