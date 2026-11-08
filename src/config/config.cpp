#include "config/config.hpp"

#include "ollama/endpoint.hpp"

#include <charconv>
#include <cstdlib>
#include <string>
#include <system_error>

namespace search::config {
namespace {

template <typename T> T parse_integer(std::string_view name, const std::string &raw, T min, T max) {
    T value{};
    const char *first = raw.data();
    const char *last = raw.data() + raw.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc{} || ptr != last || value < min || value > max) {
        throw ConfigError(std::string(name) + ": очікується ціле число в діапазоні [" +
                          std::to_string(min) + ", " + std::to_string(max) + "], отримано '" + raw +
                          "'");
    }
    return value;
}

std::string parse_non_empty(std::string_view name, const std::string &raw) {
    if (raw.empty()) {
        throw ConfigError(std::string(name) + ": значення не може бути порожнім");
    }
    return raw;
}

} // namespace

Config parse_config(const EnvLookup &lookup) {
    Config cfg;

    if (const auto v = lookup("SERVER_PORT")) {
        cfg.port = parse_integer<std::uint16_t>("SERVER_PORT", *v, 1, 65535);
    }
    if (const auto v = lookup("DATASET_DIR")) {
        cfg.dataset_dir = parse_non_empty("DATASET_DIR", *v);
    }
    if (const auto v = lookup("DEFAULT_INDEX_THREADS")) {
        cfg.default_index_threads =
            parse_integer<std::size_t>("DEFAULT_INDEX_THREADS", *v, 0, 1024);
    }
    if (const auto v = lookup("MAX_INDEX_THREADS")) {
        cfg.max_index_threads = parse_integer<std::size_t>("MAX_INDEX_THREADS", *v, 1, 1024);
    }
    if (cfg.default_index_threads > cfg.max_index_threads) {
        throw ConfigError("DEFAULT_INDEX_THREADS: перевищує MAX_INDEX_THREADS");
    }
    if (const auto v = lookup("SEARCH_TOP_K")) {
        cfg.search_top_k = parse_integer<std::size_t>("SEARCH_TOP_K", *v, 1, 1000);
    }
    if (const auto v = lookup("MAX_QUERY_LENGTH")) {
        cfg.max_query_length = parse_integer<std::size_t>("MAX_QUERY_LENGTH", *v, 1, 4096);
    }
    if (const auto v = lookup("OLLAMA_TIMEOUT_MS")) {
        cfg.ollama_timeout_ms = parse_integer<std::size_t>("OLLAMA_TIMEOUT_MS", *v, 1, 600000);
    }
    if (const auto v = lookup("SEARCH_WORKER_THREADS")) {
        cfg.search_worker_threads =
            parse_integer<std::size_t>("SEARCH_WORKER_THREADS", *v, 0, 1024);
    }
    if (const auto v = lookup("MAX_CONCURRENT_CONNECTIONS")) {
        cfg.max_concurrent_connections =
            parse_integer<std::size_t>("MAX_CONCURRENT_CONNECTIONS", *v, 1, 10000);
    }
    if (const auto v = lookup("MAX_EXPANDED_TERMS")) {
        cfg.max_expanded_terms = parse_integer<std::size_t>("MAX_EXPANDED_TERMS", *v, 1, 50);
    }
    if (const auto v = lookup("SNIPPET_MAX_CHARS")) {
        cfg.snippet_max_chars = parse_integer<std::size_t>("SNIPPET_MAX_CHARS", *v, 1, 10000);
    }
    if (const auto v = lookup("MAX_REQUEST_BODY_BYTES")) {
        cfg.max_request_body_bytes =
            parse_integer<std::size_t>("MAX_REQUEST_BODY_BYTES", *v, 1, std::size_t{1} << 30);
    }
    if (const auto v = lookup("HEALTH_CHECK_TIMEOUT_MS")) {
        cfg.health_check_timeout_ms =
            parse_integer<std::size_t>("HEALTH_CHECK_TIMEOUT_MS", *v, 1, 3600000);
    }
    if (const auto v = lookup("REQUEST_TIMEOUT_MS")) {
        cfg.request_timeout_ms = parse_integer<std::size_t>("REQUEST_TIMEOUT_MS", *v, 1, 3600000);
    }
    if (const auto v = lookup("OLLAMA_URL")) {
        cfg.ollama_url = parse_non_empty("OLLAMA_URL", *v);
    }
    if (const auto v = lookup("OLLAMA_MODEL")) {
        cfg.ollama_model = *v;
    }
    if (!ollama::parse_ollama_url(cfg.ollama_url, cfg.ollama_model)) {
        throw ConfigError("OLLAMA_URL: очікується http://хост[:порт] без шляху, отримано '" +
                          cfg.ollama_url + "'");
    }
    return cfg;
}

Config load_config_from_environment() {
    return parse_config([](std::string_view name) -> std::optional<std::string> {
        const std::string key(name);
        if (const char *value = std::getenv(key.c_str())) {
            return std::string(value);
        }
        return std::nullopt;
    });
}

} // namespace search::config
