// Тести WebSocket /ws/search (TCK-022): реальний клієнт Boost.Beast проти запущеного сервера.
// Перевіряють рукостискання, порядок етапів query_expansion → keyword_search → done, валідацію
// повідомлень, перевірку індексу до розширення і fallback розширення (FR-021, AC-021.1).
#include "index/index_manager.hpp"
#include "query/search_service.hpp"
#include "server/http_server.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace json = boost::json;
using tcp = net::ip::tcp;
using search::query::SearchOptions;
using search::query::SearchService;

class WsSearchTest : public ::testing::Test {
  protected:
    void SetUp() override {
        const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        dataset_ = std::filesystem::temp_directory_path() /
                   ("ws_search_" + name + "_" + std::to_string(getpid()));
        std::filesystem::remove_all(dataset_);
        std::filesystem::create_directories(dataset_);
        { std::ofstream(dataset_ / "a.txt") << "alpha gamma delta\n"; }
        { std::ofstream(dataset_ / "b.txt") << "alpha alpha beta\n"; }
        { std::ofstream(dataset_ / "c.txt") << "unrelated words here\n"; }
        search_ = std::make_unique<SearchService>([this] { return manager_.snapshot(); }, dataset_,
                                                  SearchOptions{10, 200});
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

    // Запускає сервер. build=false — індекс не побудовано (index_not_ready).
    void start(bool build) {
        if (build) {
            ASSERT_TRUE(
                manager_.start_build(dataset_, 2, [](const search::index::BuildResult &) {}));
            manager_.wait_for_build();
        }
        search::server::ServerOptions options;
        options.port = 0;
        options.request_timeout = std::chrono::seconds(5);
        options.index_ready = [this] {
            const auto state = manager_.state();
            return state == search::index::IndexState::Ready ||
                   state == search::index::IndexState::Rebuilding;
        };
        options.search_handler = [this](std::string_view text,
                                        const std::vector<std::string> &extra) {
            return search_->search(text, extra);
        };
        options.expansion_handler = [this](std::string,
                                           std::function<void(std::vector<std::string>)> done) {
            ++expansion_calls_;
            done(expansion_terms_);
        };
        server_ = std::make_unique<search::server::HttpServer>(ioc_, options);
        server_->start();
        port_ = server_->port();
        worker_ = std::thread([this] { ioc_.run(); });
    }

    // Клієнт WebSocket з блокуючими викликами (сервер працює в окремому потоці).
    std::unique_ptr<websocket::stream<tcp::socket>> connect() {
        auto ws = std::make_unique<websocket::stream<tcp::socket>>(client_ioc_);
        ws->next_layer().connect({net::ip::make_address("127.0.0.1"), port_});
        ws->handshake("127.0.0.1", "/ws/search");
        return ws;
    }

    static json::object read_message(websocket::stream<tcp::socket> &ws) {
        beast::flat_buffer buffer;
        ws.read(buffer);
        return json::parse(beast::buffers_to_string(buffer.data())).as_object();
    }

    static std::string stage_of(const json::object &message) {
        return std::string(message.at("stage").as_string());
    }

    std::vector<std::string> expansion_terms_;
    std::atomic<int> expansion_calls_{0};
    std::filesystem::path dataset_;
    search::index::IndexManager manager_;
    std::unique_ptr<SearchService> search_;
    net::io_context ioc_;
    net::io_context client_ioc_;
    std::unique_ptr<search::server::HttpServer> server_;
    std::thread worker_;
    std::uint16_t port_ = 0;
};

} // namespace

// AC-022.1, AC-022.2: handshake успішний; валідний запит дає три етапи в порядку.
TEST_F(WsSearchTest, PipelineEmitsStagesInOrder) {
    expansion_terms_ = {"gamma"};
    start(/*build=*/true);
    auto ws = connect();
    ws->write(net::buffer(std::string(R"({"query": "beta"})")));

    const auto expansion = read_message(*ws);
    EXPECT_EQ(stage_of(expansion), "query_expansion");
    EXPECT_EQ(expansion.at("data").as_object().at("original").as_string(), "beta");
    const auto &terms = expansion.at("data").as_object().at("expanded_terms").as_array();
    ASSERT_EQ(terms.size(), 1u);
    EXPECT_EQ(terms[0].as_string(), "gamma");

    const auto keyword = read_message(*ws);
    EXPECT_EQ(stage_of(keyword), "keyword_search");
    const auto &data = keyword.at("data").as_object();
    EXPECT_TRUE(data.at("time_ms").is_int64() || data.at("time_ms").is_uint64());
    ASSERT_FALSE(data.at("results").as_array().empty());
    const auto &first = data.at("results").as_array()[0].as_object();
    EXPECT_TRUE(first.at("doc_id").is_string());
    EXPECT_TRUE(first.at("score").is_double());
    EXPECT_TRUE(first.at("snippet").is_string());

    const auto done = read_message(*ws);
    EXPECT_EQ(stage_of(done), "done");
    EXPECT_EQ(done.size(), 1u) << "done має бути рівно {\"stage\":\"done\"} (AD-09)";
}

// AC-022.3 [P, OQ-12]: після done з'єднання відкрите, другий запит дає повний пайплайн.
TEST_F(WsSearchTest, SecondQueryAfterDoneRunsFullPipeline) {
    start(/*build=*/true);
    auto ws = connect();
    for (const char *query : {"alpha", "beta"}) {
        ws->write(net::buffer(std::string(R"({"query": ")") + query + R"("})"));
        EXPECT_EQ(stage_of(read_message(*ws)), "query_expansion");
        EXPECT_EQ(stage_of(read_message(*ws)), "keyword_search");
        EXPECT_EQ(stage_of(read_message(*ws)), "done");
    }
}

// WS-E-04 invalid_json: помилка, з'єднання залишається відкритим.
TEST_F(WsSearchTest, InvalidJsonReturnsErrorAndKeepsConnection) {
    start(/*build=*/true);
    auto ws = connect();
    ws->write(net::buffer(std::string("not json")));
    const auto error = read_message(*ws);
    EXPECT_EQ(stage_of(error), "error");
    EXPECT_EQ(error.at("data").as_object().at("code").as_string(), "invalid_json");

    ws->write(net::buffer(std::string(R"({"query": "alpha"})")));
    EXPECT_EQ(stage_of(read_message(*ws)), "query_expansion");
}

// WS-E-04 invalid_query: порожній запит після обрізання пробілів.
TEST_F(WsSearchTest, BlankQueryReturnsInvalidQuery) {
    start(/*build=*/true);
    auto ws = connect();
    ws->write(net::buffer(std::string(R"({"query": "   "})")));
    const auto error = read_message(*ws);
    EXPECT_EQ(stage_of(error), "error");
    EXPECT_EQ(error.at("data").as_object().at("code").as_string(), "invalid_query");
    EXPECT_EQ(expansion_calls_.load(), 0);
}

// FR-018 / 06 §4.5: без індексу — index_not_ready до розширення; query_expansion не надсилається.
TEST_F(WsSearchTest, IndexNotReadyReturnsErrorBeforeExpansion) {
    start(/*build=*/false);
    auto ws = connect();
    ws->write(net::buffer(std::string(R"({"query": "alpha"})")));
    const auto error = read_message(*ws);
    EXPECT_EQ(stage_of(error), "error");
    EXPECT_EQ(error.at("data").as_object().at("code").as_string(), "index_not_ready");
    EXPECT_EQ(expansion_calls_.load(), 0) << "Ollama не має викликатись без індексу";
}

// AC-021.1 на рівні WS: розширення недоступне (порожній список) → query_expansion з [],
// далі keyword_search і done.
TEST_F(WsSearchTest, ExpansionFallbackStillStreamsKeywordAndDone) {
    expansion_terms_ = {};
    start(/*build=*/true);
    auto ws = connect();
    ws->write(net::buffer(std::string(R"({"query": "alpha"})")));

    const auto expansion = read_message(*ws);
    EXPECT_EQ(stage_of(expansion), "query_expansion");
    EXPECT_TRUE(expansion.at("data").as_object().at("expanded_terms").as_array().empty());
    EXPECT_EQ(stage_of(read_message(*ws)), "keyword_search");
    EXPECT_EQ(stage_of(read_message(*ws)), "done");
}
