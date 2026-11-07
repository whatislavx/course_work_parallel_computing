#include "config/config.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using search::config::ConfigError;
using search::config::EnvLookup;
using search::config::parse_config;

EnvLookup env_from(std::vector<std::pair<std::string, std::string>> vars) {
    return [vars = std::move(vars)](std::string_view name) -> std::optional<std::string> {
        for (const auto &[key, value] : vars) {
            if (key == name) {
                return value;
            }
        }
        return std::nullopt;
    };
}

} // namespace

TEST(Config, DefaultsWhenNothingSet) {
    const auto cfg = parse_config(env_from({}));
    EXPECT_EQ(cfg.port, 8080);
    EXPECT_EQ(cfg.default_index_threads, 0U);
    EXPECT_EQ(cfg.max_request_body_bytes, std::size_t{1} << 20);
    EXPECT_EQ(cfg.ollama_url, "http://ollama:11434");
    EXPECT_TRUE(cfg.dataset_dir.empty());
}

TEST(Config, ValidOverridesAreApplied) {
    const auto cfg = parse_config(env_from({{"SERVER_PORT", "9000"},
                                            {"DEFAULT_INDEX_THREADS", "0"},
                                            {"DATASET_DIR", "/data/docs"},
                                            {"OLLAMA_URL", "http://localhost:11434"}}));
    EXPECT_EQ(cfg.port, 9000);
    EXPECT_EQ(cfg.default_index_threads, 0U);
    EXPECT_EQ(cfg.dataset_dir, "/data/docs");
    EXPECT_EQ(cfg.ollama_url, "http://localhost:11434");
}

TEST(Config, PortOutOfRangeIsRejectedWithVariableName) {
    try {
        parse_config(env_from({{"SERVER_PORT", "70000"}}));
        FAIL() << "expected ConfigError";
    } catch (const ConfigError &e) {
        EXPECT_NE(std::string(e.what()).find("SERVER_PORT"), std::string::npos);
    }
}

TEST(Config, PortZeroIsRejected) {
    EXPECT_THROW(parse_config(env_from({{"SERVER_PORT", "0"}})), ConfigError);
}

TEST(Config, NonNumericValueIsRejected) {
    EXPECT_THROW(parse_config(env_from({{"SERVER_PORT", "abc"}})), ConfigError);
    EXPECT_THROW(parse_config(env_from({{"DEFAULT_INDEX_THREADS", "4x"}})), ConfigError);
}

TEST(Config, NegativeValueIsRejected) {
    EXPECT_THROW(parse_config(env_from({{"MAX_REQUEST_BODY_BYTES", "-1"}})), ConfigError);
}

TEST(Config, EmptyStringValueIsRejected) {
    EXPECT_THROW(parse_config(env_from({{"OLLAMA_URL", ""}})), ConfigError);
    EXPECT_THROW(parse_config(env_from({{"DATASET_DIR", ""}})), ConfigError);
}

TEST(Config, RequestTimeoutDefaultAndOverride) {
    EXPECT_EQ(parse_config(env_from({})).request_timeout_ms, 10000U);
    EXPECT_EQ(parse_config(env_from({{"REQUEST_TIMEOUT_MS", "500"}})).request_timeout_ms, 500U);
    EXPECT_THROW(parse_config(env_from({{"REQUEST_TIMEOUT_MS", "0"}})), ConfigError);
}
