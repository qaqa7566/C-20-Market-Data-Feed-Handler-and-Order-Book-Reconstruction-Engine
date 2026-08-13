#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Minimal command-line flag parser shared by the CLI apps. Supports
// "--key value" and "--flag". Not trying to be a real arg library.
namespace mdfh::app {

class Args {
public:
    Args(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            std::string_view a = argv[i];
            if (a.rfind("--", 0) == 0) {
                std::string key(a.substr(2));
                if (i + 1 < argc && std::string_view(argv[i + 1]).rfind("--", 0) != 0) {
                    kv_[key] = argv[++i];
                } else {
                    kv_[key] = "";  // boolean flag
                }
            }
        }
    }

    [[nodiscard]] bool has(const std::string& k) const { return kv_.count(k) > 0; }

    [[nodiscard]] std::string str(const std::string& k, const std::string& def = "") const {
        auto it = kv_.find(k);
        return it == kv_.end() ? def : it->second;
    }

    [[nodiscard]] std::uint64_t u64(const std::string& k, std::uint64_t def) const {
        auto it = kv_.find(k);
        if (it == kv_.end() || it->second.empty()) return def;
        std::uint64_t v = def;
        std::from_chars(it->second.data(), it->second.data() + it->second.size(), v);
        return v;
    }

    [[nodiscard]] double f64(const std::string& k, double def) const {
        auto it = kv_.find(k);
        if (it == kv_.end() || it->second.empty()) return def;
        try { return std::stod(it->second); } catch (...) { return def; }
    }

private:
    std::unordered_map<std::string, std::string> kv_;
};

// Parse "host:port" into components.
inline bool parse_host_port(const std::string& s, std::string& host, std::uint16_t& port) {
    auto colon = s.rfind(':');
    if (colon == std::string::npos) return false;
    host = s.substr(0, colon);
    port = static_cast<std::uint16_t>(std::stoul(s.substr(colon + 1)));
    return true;
}

}  // namespace mdfh::app
