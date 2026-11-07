#include "ollama/endpoint.hpp"

#include <charconv>
#include <cstdint>

namespace search::ollama {
namespace {

bool is_valid_port(std::string_view port) {
    std::uint32_t value = 0;
    const char *first = port.data();
    const char *last = port.data() + port.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    return ec == std::errc{} && ptr == last && value >= 1 && value <= 65535;
}

} // namespace

std::optional<Endpoint> parse_ollama_url(std::string_view url, std::string_view model) {
    constexpr std::string_view kScheme = "http://";
    if (url.substr(0, kScheme.size()) != kScheme) {
        return std::nullopt;
    }
    const std::string_view rest = url.substr(kScheme.size());
    const auto slash = rest.find('/');
    if (slash != std::string_view::npos && rest.substr(slash) != "/") {
        return std::nullopt; // шляхи не підтримуються
    }
    const std::string_view authority = rest.substr(0, slash);
    std::string_view host = authority;
    std::string port = "80";
    const auto colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        host = authority.substr(0, colon);
        port = std::string(authority.substr(colon + 1));
        if (!is_valid_port(port)) {
            return std::nullopt;
        }
    }
    if (host.empty()) {
        return std::nullopt;
    }
    return Endpoint{std::string(host), port, std::string(model)};
}

} // namespace search::ollama
