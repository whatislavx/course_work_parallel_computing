// Тести консольного клієнта (TCK-025): аргументи, адреса, вивід і коди виходу (FR-029, FR-030).
#include "cli/search_cli.hpp"
#include "index/index_manager.hpp"
#include "query/search_service.hpp"
#include "server/http_server.hpp"

#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>

#include <chrono>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

namespace net = boost::asio;
using search::cli::CliOptions;
using search::cli::format_results;
using search::cli::parse_arguments;
using search::cli::parse_server_url;
using search::cli::run_search;

TEST(CliArguments, QueryAndUrlAreParsed) {
    CliOptions options;
    EXPECT_EQ(parse_arguments({"--url", "http://h:9", "parallel"}, &options), "");
    EXPECT_EQ(options.query, "parallel");
    EXPECT_EQ(options.server_url, "http://h:9");
}

TEST(CliArguments, MissingQueryIsUsageError) {
    CliOptions options;
    EXPECT_NE(parse_arguments({}, &options), "");
}

TEST(CliArguments, UnknownOptionAndExtraArgumentAreUsageErrors) {
    CliOptions options;
    EXPECT_NE(parse_arguments({"--bogus", "q"}, &options), "");
    EXPECT_NE(parse_arguments({"a", "b"}, &options), "");
    EXPECT_NE(parse_arguments({"--url"}, &options), "");
}

TEST(CliArguments, HelpNeedsNoQuery) {
    CliOptions options;
    EXPECT_EQ(parse_arguments({"--help"}, &options), "");
    EXPECT_TRUE(options.help);
}

TEST(CliServerUrl, AcceptsHostAndPortOnly) {
    const auto address = parse_server_url("http://127.0.0.1:8080");
    ASSERT_TRUE(address.has_value());
    EXPECT_EQ(address->host, "127.0.0.1");
    EXPECT_EQ(address->port, "8080");
    EXPECT_FALSE(parse_server_url("https://h:1").has_value());
    EXPECT_FALSE(parse_server_url("http://h").has_value());
    EXPECT_FALSE(parse_server_url("http://h:port").has_value());
    EXPECT_FALSE(parse_server_url("http://h:1/api").has_value());
}

TEST(CliFormat, PrintsRankDocIdScoreAndSnippet) {
    const std::string body =
        R"({"results":[{"doc_id":"b.txt","score":1.5,"snippet":"alpha alpha beta"}],"time_ms":1})";
    EXPECT_EQ(format_results(body, "alpha"), "1. b.txt (score 1.5000)\n   alpha alpha beta\n");
}

TEST(CliFormat, EmptyResultsPrintsNoResultsMessage) {
    EXPECT_EQ(format_results(R"({"results":[],"time_ms":0})", "zzz"),
              "No results found for \"zzz\".\n");
}

// Пошук проти справжнього сервера: успіх, порожній результат, помилка сервера, недоступність.
class CliRunTest : public ::testing::Test {
  protected:
    void SetUp() override {
        dataset_ =
            std::filesystem::temp_directory_path() / ("cli_test_" + std::to_string(getpid()));
        std::filesystem::remove_all(dataset_);
        std::filesystem::create_directories(dataset_);
        { std::ofstream(dataset_ / "a.txt") << "alpha gamma delta\n"; }
        { std::ofstream(dataset_ / "b.txt") << "alpha alpha beta\n"; }
        service_ = std::make_unique<search::query::SearchService>(
            [this] { return manager_.snapshot(); }, dataset_,
            search::query::SearchOptions{10, 200});
    }

    void TearDown() override {
        if (server_) {
            net::post(ioc_, [this] {
                server_->stop();
                ioc_.stop();
            });
            worker_.join();
        }
        std::filesystem::remove_all(dataset_);
    }

    void start(bool build) {
        if (build) {
            ASSERT_TRUE(
                manager_.start_build(dataset_, 2, [](const search::index::BuildResult &) {}));
            manager_.wait_for_build();
        }
        search::server::ServerOptions options;
        options.port = 0;
        options.request_timeout = std::chrono::seconds(5);
        options.search_handler = [this](std::string_view text,
                                        const std::vector<std::string> &extra) {
            return service_->search(text, extra);
        };
        options.index_ready = [this] {
            return manager_.state() == search::index::IndexState::Ready;
        };
        server_ = std::make_unique<search::server::HttpServer>(ioc_, options);
        server_->start();
        url_ = "http://127.0.0.1:" + std::to_string(server_->port());
        worker_ = std::thread([this] { ioc_.run(); });
    }

    std::filesystem::path dataset_;
    search::index::IndexManager manager_;
    std::unique_ptr<search::query::SearchService> service_;
    net::io_context ioc_;
    std::unique_ptr<search::server::HttpServer> server_;
    std::thread worker_;
    std::string url_;
};

TEST_F(CliRunTest, SuccessPrintsResultsAndExitsZero) {
    start(/*build=*/true);
    CliOptions options;
    options.server_url = url_;
    options.query = "alpha";
    std::ostringstream out, err;
    EXPECT_EQ(run_search(options, out, err), 0);
    EXPECT_NE(out.str().find("b.txt"), std::string::npos) << out.str();
    EXPECT_NE(out.str().find("a.txt"), std::string::npos) << out.str();
    EXPECT_TRUE(err.str().empty());
}

TEST_F(CliRunTest, NoMatchPrintsMessageAndExitsZero) {
    start(/*build=*/true);
    CliOptions options;
    options.server_url = url_;
    options.query = "zzzxqqnotaword";
    std::ostringstream out, err;
    EXPECT_EQ(run_search(options, out, err), 0);
    EXPECT_NE(out.str().find("No results found"), std::string::npos);
}

TEST_F(CliRunTest, ServerErrorPrintsMessageAndExitsNonZero) {
    start(/*build=*/false); // індекс не побудовано: 409 index_not_ready
    CliOptions options;
    options.server_url = url_;
    options.query = "parallel";
    std::ostringstream out, err;
    EXPECT_NE(run_search(options, out, err), 0);
    EXPECT_NE(err.str().find("index is not built yet"), std::string::npos) << err.str();
    EXPECT_TRUE(out.str().empty());
}

TEST(CliRun, UnavailableServerExitsNonZero) {
    CliOptions options;
    // Порт, на якому ніхто не слухає: закриваємо тимчасовий acceptor.
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::acceptor acceptor(ioc, {boost::asio::ip::tcp::v4(), 0});
    options.server_url = "http://127.0.0.1:" + std::to_string(acceptor.local_endpoint().port());
    acceptor.close();
    options.query = "parallel";
    std::ostringstream out, err;
    EXPECT_NE(run_search(options, out, err), 0);
    EXPECT_NE(err.str().find("unavailable"), std::string::npos) << err.str();
}

} // namespace
