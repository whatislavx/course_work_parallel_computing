// Тести EP-03 (POST /api/search): парсер тіла, сервіс пошуку і HTTP-контракт (TCK-017).
#include "index/index_manager.hpp"
#include "query/search_service.hpp"
#include "server/http_server.hpp"
#include "server/router.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using search::query::IndexNotReadyError;
using search::query::SearchOptions;
using search::query::SearchService;
using search::server::parse_search_body;
using search::server::SearchRequest;

// Датасет: три документи з різною частотою терма «alpha».
std::filesystem::path make_dataset(const std::string &name) {
    const auto dir = std::filesystem::temp_directory_path() / ("search_endpoint_" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    { std::ofstream(dir / "a.txt") << "alpha gamma delta\n"; }
    { std::ofstream(dir / "b.txt") << "alpha alpha beta\n"; }
    { std::ofstream(dir / "c.txt") << "unrelated words here\n"; }
    return dir;
}

search::server::Response parse_error(std::string_view body, std::size_t limit = 256) {
    auto parsed = parse_search_body(body, limit);
    EXPECT_TRUE(std::holds_alternative<search::server::Response>(parsed)) << body;
    return std::get<search::server::Response>(std::move(parsed));
}

std::string error_code_of(const search::server::Response &res) {
    return boost::json::parse(res.body())
        .as_object()
        .at("error")
        .as_object()
        .at("code")
        .as_string()
        .c_str();
}

} // namespace

// --- Парсер тіла ---

TEST(SearchBody, ValidKeywordRequestIsTrimmed) {
    auto parsed = parse_search_body(R"({"query": "  alpha beta \n", "mode": "keyword"})", 256);
    ASSERT_TRUE(std::holds_alternative<SearchRequest>(parsed));
    EXPECT_EQ(std::get<SearchRequest>(parsed).query, "alpha beta");
}

TEST(SearchBody, MalformedOrNonObjectBodyIsInvalidJson) {
    EXPECT_EQ(error_code_of(parse_error("{not json")), "invalid_json");
    EXPECT_EQ(error_code_of(parse_error("[1,2]")), "invalid_json");
    EXPECT_EQ(error_code_of(parse_error("")), "invalid_json");
}

TEST(SearchBody, MissingOrNonStringOrBlankQueryIsInvalidQuery) {
    EXPECT_EQ(error_code_of(parse_error(R"({"mode": "keyword"})")), "invalid_query");
    EXPECT_EQ(error_code_of(parse_error(R"({"query": 5, "mode": "keyword"})")), "invalid_query");
    EXPECT_EQ(error_code_of(parse_error(R"({"query": "   ", "mode": "keyword"})")),
              "invalid_query");
}

TEST(SearchBody, QueryLongerThanLimitIsInvalidQuery) {
    EXPECT_EQ(error_code_of(parse_error(R"({"query": "abcdef", "mode": "keyword"})", 5)),
              "invalid_query");
}

TEST(SearchBody, MissingOrUnknownModeIsInvalidMode) {
    EXPECT_EQ(error_code_of(parse_error(R"({"query": "a"})")), "invalid_mode");
    EXPECT_EQ(error_code_of(parse_error(R"({"query": "a", "mode": "fuzzy"})")), "unsupported_mode");
    EXPECT_EQ(error_code_of(parse_error(R"({"query": "a", "mode": 1})")), "invalid_mode");
}

TEST(SearchBody, ExpandedModeIsAcceptedAndFlagged) {
    auto parsed = parse_search_body(R"({"query": "a", "mode": "expanded"})", 256);
    ASSERT_TRUE(std::holds_alternative<SearchRequest>(parsed));
    EXPECT_TRUE(std::get<SearchRequest>(parsed).expanded);
}

// --- Сервіс пошуку ---

TEST(SearchService, NoIndexThrowsIndexNotReady) {
    SearchService service([] { return nullptr; }, {}, SearchOptions{});
    EXPECT_THROW(service.search("alpha"), IndexNotReadyError);
}

TEST(SearchService, EmptyTokenQueryReturnsNoResults) {
    search::index::SharedInvertedIndex index;
    index.add_document("a.txt", {"alpha"});
    SearchService service(
        [&index] {
            return std::shared_ptr<const search::index::SharedInvertedIndex>(
                &index, [](const search::index::SharedInvertedIndex *) {});
        },
        {}, SearchOptions{});
    EXPECT_TRUE(service.search("  !!! ").empty());
}

TEST(SearchService, ResultsAreRankedWithSnippetsAndTopKLimit) {
    const auto dataset = make_dataset("ranked");
    auto index = std::make_shared<search::index::SharedInvertedIndex>();
    index->add_document("a.txt", {"alpha", "gamma", "delta"});
    index->add_document("b.txt", {"alpha", "alpha", "beta"});
    index->add_document("c.txt", {"unrelated", "words", "here"});
    SearchService service([index] { return index; }, dataset,
                          SearchOptions{/*top_k=*/1, /*snippet_max_chars=*/40});
    const auto hits = service.search("alpha");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].doc_id, "b.txt");
    EXPECT_GT(hits[0].score, 0.0);
    EXPECT_EQ(hits[0].snippet, "alpha alpha beta");
    std::filesystem::remove_all(dataset);
}

TEST(SearchService, MissingFileYieldsEmptySnippetWithoutFailing) {
    auto index = std::make_shared<search::index::SharedInvertedIndex>();
    index->add_document("gone.txt", {"alpha"});
    index->add_document("other.txt", {"beta"});
    SearchService service([index] { return index; }, "/nonexistent-dataset-dir",
                          SearchOptions{10, 200});
    const auto hits = service.search("alpha");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].doc_id, "gone.txt");
    EXPECT_EQ(hits[0].snippet, "");
}

TEST(SearchService, DocIdEscapingDatasetDirIsNotRead) {
    const auto dataset = make_dataset("traversal");
    auto index = std::make_shared<search::index::SharedInvertedIndex>();
    index->add_document("../search_endpoint_traversal/a.txt", {"alpha"});
    SearchService service([index] { return index; }, dataset, SearchOptions{10, 200});
    const auto hits = service.search("alpha");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].snippet, "");
    std::filesystem::remove_all(dataset);
}

// --- HTTP-контракт EP-03 ---

class SearchEndpointTest : public ::testing::Test {
  protected:
    void TearDown() override {
        if (server_) {
            net::post(ioc_, [this] {
                server_->stop();
                ioc_.stop();
            });
            worker_.join();
        }
        if (!dataset_.empty()) {
            std::filesystem::remove_all(dataset_);
        }
    }

    void start_server() {
        dataset_ = make_dataset("http");
        search_service_ = std::make_unique<SearchService>([this] { return manager_.snapshot(); },
                                                          dataset_, SearchOptions{10, 200});
        search::server::ServerOptions options;
        options.port = 0;
        options.request_timeout = std::chrono::seconds(5);
        options.index_start = [this](std::size_t threads, search::index::BuildCallback done) {
            return manager_.start_build(dataset_, threads, std::move(done));
        };
        options.default_index_threads = 2;
        options.max_index_threads = 8;
        options.search_handler = [this](std::string_view text,
                                        const std::vector<std::string> &extra) {
            return search_service_->search(text, extra);
        };
        if (expansion_terms_) {
            options.expansion_handler = [this](std::string,
                                               std::function<void(std::vector<std::string>)> done) {
                done(*expansion_terms_);
            };
        }
        options.max_query_length = 256;
        server_ = std::make_unique<search::server::HttpServer>(ioc_, options);
        server_->start();
        port_ = server_->port();
        worker_ = std::thread([this] { ioc_.run(); });
    }

    void build_index() {
        http::response<http::string_body> res;
        ASSERT_NO_THROW(res = request(http::verb::post, "/api/index", R"({"threads": 2})"));
        ASSERT_EQ(res.result(), http::status::ok);
    }

    http::response<http::string_body> request(http::verb verb, const std::string &target,
                                              const std::string &body = "") {
        net::io_context client_ioc;
        tcp::socket socket(client_ioc);
        socket.connect({net::ip::make_address("127.0.0.1"), port_});
        http::request<http::string_body> req{verb, target, 11};
        req.set(http::field::host, "127.0.0.1");
        req.set(http::field::content_type, "application/json");
        req.body() = body;
        req.prepare_payload();
        http::write(socket, req);
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(socket, buffer, res);
        return res;
    }

    std::optional<std::vector<std::string>> expansion_terms_;
    std::filesystem::path dataset_;
    search::index::IndexManager manager_;
    std::unique_ptr<SearchService> search_service_;
    net::io_context ioc_;
    std::unique_ptr<search::server::HttpServer> server_;
    std::thread worker_;
    std::uint16_t port_ = 0;
};

TEST_F(SearchEndpointTest, SearchBeforeBuildReturnsIndexNotReady) {
    start_server();
    const auto res =
        request(http::verb::post, "/api/search", R"({"query": "alpha", "mode": "keyword"})");
    EXPECT_EQ(res.result(), http::status::conflict);
    EXPECT_EQ(error_code_of(res), "index_not_ready");
}

TEST_F(SearchEndpointTest, SearchAfterBuildReturnsResultsWithContractFields) {
    start_server();
    build_index();
    const auto res =
        request(http::verb::post, "/api/search", R"({"query": "alpha", "mode": "keyword"})");
    ASSERT_EQ(res.result(), http::status::ok);
    const auto body = boost::json::parse(res.body()).as_object();
    const auto &results = body.at("results").as_array();
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].as_object().at("doc_id").as_string(), "b.txt");
    EXPECT_TRUE(results[0].as_object().at("score").is_double());
    EXPECT_TRUE(results[0].as_object().at("snippet").is_string());
    EXPECT_GE(body.at("time_ms").as_int64(), 0);
    EXPECT_EQ(body.size(), 2u);
}

TEST_F(SearchEndpointTest, NoMatchReturnsEmptyResults) {
    start_server();
    build_index();
    const auto res =
        request(http::verb::post, "/api/search", R"({"query": "zeta", "mode": "keyword"})");
    ASSERT_EQ(res.result(), http::status::ok);
    EXPECT_TRUE(boost::json::parse(res.body()).as_object().at("results").as_array().empty());
}

TEST_F(SearchEndpointTest, InvalidBodyReturnsBadRequestWithCode) {
    start_server();
    build_index();
    const auto res =
        request(http::verb::post, "/api/search", R"({"query": "", "mode": "keyword"})");
    EXPECT_EQ(res.result(), http::status::bad_request);
    EXPECT_EQ(error_code_of(res), "invalid_query");
}

// expanded без обробника розширення — пошук без розширення (FR-021).
TEST_F(SearchEndpointTest, ExpandedWithoutExpansionFallsBackToKeyword) {
    start_server();
    build_index();
    const auto res =
        request(http::verb::post, "/api/search", R"({"query": "alpha", "mode": "expanded"})");
    ASSERT_EQ(res.result(), http::status::ok);
    EXPECT_EQ(boost::json::parse(res.body()).as_object().at("results").as_array().size(), 2u);
}

// expanded з розширенням: терм «gamma» з розширення знаходить a.txt, якого немає за «zeta».
TEST_F(SearchEndpointTest, ExpandedTermsAffectRanking) {
    expansion_terms_ = std::vector<std::string>{"gamma"};
    start_server();
    build_index();
    const auto res =
        request(http::verb::post, "/api/search", R"({"query": "zeta", "mode": "expanded"})");
    ASSERT_EQ(res.result(), http::status::ok);
    const boost::json::value parsed = boost::json::parse(res.body());
    const auto &results = parsed.as_object().at("results").as_array();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].as_object().at("doc_id").as_string(), "a.txt");
}

TEST_F(SearchEndpointTest, GetOnSearchReturnsMethodNotAllowed) {
    start_server();
    const auto res = request(http::verb::get, "/api/search");
    EXPECT_EQ(res.result(), http::status::method_not_allowed);
    EXPECT_EQ(res[http::field::allow], "POST");
}
