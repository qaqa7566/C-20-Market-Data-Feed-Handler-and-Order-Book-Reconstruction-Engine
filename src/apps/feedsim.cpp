// mdfh_feedsim -- synthetic exchange feed generator.
//
// Examples:
//   mdfh_feedsim --messages 1000000 --symbols 8 --seed 42 --out data/feed.cap
//   mdfh_feedsim --messages 500000 --tcp 9001          (serve one TCP client)
//   mdfh_feedsim --messages 500000 --udp 127.0.0.1:9002
#include <array>
#include <cstdio>
#include <string>

#include "app_common.hpp"
#include "mdfh/capture.hpp"
#include "mdfh/net/tcp.hpp"
#include "mdfh/net/udp.hpp"
#include "mdfh/serialization.hpp"
#include "mdfh/simulator.hpp"

using namespace mdfh;

int main(int argc, char** argv) {
    app::Args args(argc, argv);

    SimConfig cfg;
    cfg.num_messages = args.u64("messages", 1'000'000);
    cfg.num_symbols  = static_cast<std::uint32_t>(args.u64("symbols", 4));
    cfg.seed         = args.u64("seed", 42);

    const std::string out = args.str("out");
    const bool use_tcp = args.has("tcp");
    const bool use_udp = args.has("udp");

    if (out.empty() && !use_tcp && !use_udp) {
        std::fprintf(stderr,
                     "usage: mdfh_feedsim [--messages N] [--symbols S] [--seed X]\n"
                     "                    (--out FILE | --tcp PORT | --udp HOST:PORT)\n");
        return 2;
    }

    MarketSimulator sim(cfg);

    if (!out.empty()) {
        CaptureWriter w;
        if (!w.open(out)) { std::fprintf(stderr, "cannot open %s\n", out.c_str()); return 1; }
        auto n = sim.run([&](Sequence s, TimestampNs t, const auto& body) { w.write(s, t, body); });
        w.close();
        std::printf("wrote %llu messages to %s\n", (unsigned long long)n, out.c_str());
        return 0;
    }

    if (use_tcp) {
        net::TcpServer server;
        std::uint16_t port = static_cast<std::uint16_t>(args.u64("tcp", 0));
        if (!server.listen(port)) { std::fprintf(stderr, "listen failed\n"); return 1; }
        std::printf("TCP feed listening on 127.0.0.1:%u -- waiting for client...\n", server.port());
        auto conn = server.accept();
        if (!conn) { std::fprintf(stderr, "accept failed\n"); return 1; }
        std::printf("client connected; streaming...\n");
        std::uint64_t n = 0;
        bool ok = true;
        sim.run([&](Sequence s, TimestampNs t, const auto& body) {
            if (!ok) return;
            std::array<std::byte, kMaxMessageSize> buf{};
            std::size_t len = encode_message(buf, s, t, body);
            ok = net::send_all(conn->fd(), {buf.data(), len});
            ++n;
        });
        std::printf("streamed %llu messages (ok=%d)\n", (unsigned long long)n, ok ? 1 : 0);
        return ok ? 0 : 1;
    }

    // UDP
    std::string host; std::uint16_t port = 0;
    if (!app::parse_host_port(args.str("udp"), host, port)) {
        std::fprintf(stderr, "bad --udp HOST:PORT\n"); return 2;
    }
    net::UdpSender sender;
    if (!sender.open(host, port)) { std::fprintf(stderr, "udp open failed\n"); return 1; }
    std::printf("UDP feed -> %s:%u\n", host.c_str(), port);
    std::uint64_t n = 0;
    sim.run([&](Sequence s, TimestampNs t, const auto& body) {
        std::array<std::byte, kMaxMessageSize> buf{};
        std::size_t len = encode_message(buf, s, t, body);
        (void)sender.send({buf.data(), len});
        ++n;
    });
    std::printf("sent %llu datagrams\n", (unsigned long long)n);
    return 0;
}
