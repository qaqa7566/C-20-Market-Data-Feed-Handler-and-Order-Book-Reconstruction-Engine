// mdfh_feedhandler -- receive a live feed over TCP/UDP and reconstruct books.
//
//   mdfh_feedhandler --tcp 127.0.0.1:9001
//   mdfh_feedhandler --udp 9002 [--idle-ms 2000]
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "app_common.hpp"
#include "mdfh/book_builder.hpp"
#include "mdfh/feed_handler.hpp"
#include "mdfh/net/tcp.hpp"
#include "mdfh/net/udp.hpp"
#include "mdfh/replay.hpp"

using namespace mdfh;

namespace {
void print_summary(BookBuilder& builder, FeedHandler<BookBuilder>& feed) {
    const auto& s = feed.seq_stats();
    std::printf("=== feed handler summary ===\n");
    std::printf("forwarded/dropped/malformed : %llu / %llu / %llu\n",
                (unsigned long long)feed.forwarded(), (unsigned long long)feed.dropped(),
                (unsigned long long)feed.malformed());
    std::printf("seq in/dup/ooo/gap/missing  : %llu / %llu / %llu / %llu / %llu\n",
                (unsigned long long)s.in_order, (unsigned long long)s.duplicates,
                (unsigned long long)s.out_of_order, (unsigned long long)s.gap_events,
                (unsigned long long)s.missing);
    std::printf("symbols                     : %zu\n", builder.manager().symbol_count());
    std::printf("book invariants             : %s\n",
                builder.manager().check_all_invariants() ? "OK" : "VIOLATED");
    std::printf("book fingerprint            : %016llx\n",
                (unsigned long long)book_fingerprint(builder.manager()));
}
}  // namespace

int main(int argc, char** argv) {
    app::Args args(argc, argv);
    BookBuilder builder;
    FeedHandler<BookBuilder> feed(builder);

    if (args.has("tcp")) {
        std::string host; std::uint16_t port = 0;
        if (!app::parse_host_port(args.str("tcp"), host, port)) {
            std::fprintf(stderr, "bad --tcp HOST:PORT\n"); return 2;
        }
        net::TcpClient client;
        if (!client.connect(host, port)) { std::fprintf(stderr, "connect failed\n"); return 1; }
        std::printf("connected to %s:%u\n", host.c_str(), port);
        std::vector<std::byte> buf(64 * 1024);
        for (;;) {
            std::ptrdiff_t n = net::recv_some(client.fd(), buf);
            if (n <= 0) break;  // peer closed or error
            feed.consume({buf.data(), static_cast<std::size_t>(n)});
        }
        print_summary(builder, feed);
        return 0;
    }

    if (args.has("udp")) {
        std::uint16_t port = static_cast<std::uint16_t>(args.u64("udp", 0));
        int idle_ms = static_cast<int>(args.u64("idle-ms", 2000));
        net::UdpReceiver rx;
        if (!rx.bind(port)) { std::fprintf(stderr, "bind failed\n"); return 1; }
        (void)rx.set_recv_timeout_ms(idle_ms);
        std::printf("listening UDP on 127.0.0.1:%u (idle timeout %d ms)\n", rx.port(), idle_ms);
        std::array<std::byte, 64 * 1024> buf{};
        for (;;) {
            std::ptrdiff_t n = rx.recv(buf);
            if (n <= 0) break;  // timeout or error -> assume feed ended
            feed.consume_datagram({buf.data(), static_cast<std::size_t>(n)});
        }
        print_summary(builder, feed);
        return 0;
    }

    std::fprintf(stderr, "usage: mdfh_feedhandler (--tcp HOST:PORT | --udp PORT)\n");
    return 2;
}
