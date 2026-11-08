// Тести WebSocket /ws/search (TCK-022): реальний клієнт Boost.Beast проти запущеного сервера.
// Перевіряють рукостискання, порядок етапів query_expansion → keyword_search → done, валідацію
// повідомлень, перевірку індексу до розширення і fallback розширення (FR-021, AC-021.1).
#include "index/index_manager.hpp"
#include "pool/thread_pool.hpp"
#include "query/search_service.hpp"
#include "server/http_server.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
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
namespace http = beast::http;
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
    void start(bool build, bool use_pool = false) {
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
            if (expansion_delay_.count() == 0) {
                done(expansion_terms_);
                return;
            }
            // Затримка Ollama (BDD-WS-06, BDD-WS-08): таймер на io_context сервера.
            auto timer = std::make_shared<net::steady_timer>(ioc_, expansion_delay_);
            timer->async_wait(
                [timer, terms = expansion_terms_, done](beast::error_code) { done(terms); });
        };
        if (use_pool) {
            // Пул пошуку як у продакшні (TCK-019): пошук не виконується в io_context.
            pool_ = std::make_unique<search::pool::ThreadPool>(4, 64);
            options.search_executor = [this](std::function<void()> task) {
                pool_->submit(std::move(task));
            };
        }
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

    // Блокуючий REST-клієнт (POST з JSON), окремий io_context для кожного виклику.
    std::pair<unsigned, std::string> post_json(const std::string &target, const std::string &body) {
        net::io_context client;
        tcp::socket socket(client);
        socket.connect({net::ip::make_address("127.0.0.1"), port_});
        http::request<http::string_body> req{http::verb::post, target, 11};
        req.set(http::field::host, "127.0.0.1");
        req.set(http::field::content_type, "application/json");
        req.body() = body;
        req.prepare_payload();
        http::write(socket, req);
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(socket, buffer, res);
        return {res.result_int(), res.body()};
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
    std::chrono::milliseconds expansion_delay_{0};
    std::atomic<int> expansion_calls_{0};
    std::filesystem::path dataset_;
    search::index::IndexManager manager_;
    std::unique_ptr<SearchService> search_;
    net::io_context ioc_;
    net::io_context client_ioc_;
    std::unique_ptr<search::server::HttpServer> server_;
    std::thread worker_;
    std::uint16_t port_ = 0;
    // Оголошено останнім: знищується першим, поки пошукові залежності ще живі.
    std::unique_ptr<search::pool::ThreadPool> pool_;
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

// BDD-WS-04: усі некоректні повідомлення дають error з потрібним кодом; з'єднання відкрите.
TEST_F(WsSearchTest, InvalidClientMessagesReturnErrorCodesAndKeepConnection) {
    start(/*build=*/true);
    auto ws = connect();
    const std::string too_long(257, 'a');
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"[1, 2]", "invalid_json"},
        {R"({"q": "parallel"})", "invalid_query"},
        {R"({"query": ""})", "invalid_query"},
        {R"({"query": "   "})", "invalid_query"},
        {R"({"query": 7})", "invalid_query"},
        {R"({"query": ")" + too_long + R"("})", "invalid_query"},
    };
    for (const auto &[message, code] : cases) {
        ws->write(net::buffer(message));
        const auto error = read_message(*ws);
        EXPECT_EQ(stage_of(error), "error") << message;
        EXPECT_EQ(error.at("data").as_object().at("code").as_string(), code) << message;
    }
    ws->binary(true);
    ws->write(net::buffer(std::string("\x01\x02")));
    ws->text(true);
    const auto binary_error = read_message(*ws);
    EXPECT_EQ(binary_error.at("data").as_object().at("code").as_string(), "invalid_json");

    ws->write(net::buffer(std::string(R"({"query": "alpha"})")));
    EXPECT_EQ(stage_of(read_message(*ws)), "query_expansion");
    EXPECT_EQ(stage_of(read_message(*ws)), "keyword_search");
    EXPECT_EQ(stage_of(read_message(*ws)), "done");
    EXPECT_EQ(expansion_calls_.load(), 1) << "некоректні повідомлення не викликають Ollama";
}

// BDD-WS-08 / FR-027: повідомлення під час пайплайну отримує pipeline_busy; перший пайплайн
// завершується done; запит після done приймається.
TEST_F(WsSearchTest, QueryWhilePipelineRunsIsRejectedWithPipelineBusy) {
    expansion_delay_ = std::chrono::milliseconds(300);
    start(/*build=*/true);
    auto ws = connect();
    ws->write(net::buffer(std::string(R"({"query": "alpha"})")));
    ws->write(net::buffer(std::string(R"({"query": "beta"})")));

    const auto busy = read_message(*ws);
    EXPECT_EQ(stage_of(busy), "error");
    EXPECT_EQ(busy.at("data").as_object().at("code").as_string(), "pipeline_busy");

    const auto expansion = read_message(*ws);
    EXPECT_EQ(stage_of(expansion), "query_expansion");
    EXPECT_EQ(expansion.at("data").as_object().at("original").as_string(), "alpha");
    EXPECT_EQ(stage_of(read_message(*ws)), "keyword_search");
    EXPECT_EQ(stage_of(read_message(*ws)), "done");

    expansion_delay_ = std::chrono::milliseconds(0);
    ws->write(net::buffer(std::string(R"({"query": "beta"})")));
    EXPECT_EQ(stage_of(read_message(*ws)), "query_expansion");
    EXPECT_EQ(stage_of(read_message(*ws)), "keyword_search");
    EXPECT_EQ(stage_of(read_message(*ws)), "done");
}

// BDD-WS-06 / NFR-009: обрив клієнта A посеред пайплайну не впливає на клієнта B.
TEST_F(WsSearchTest, ClientDisconnectMidPipelineDoesNotAffectOtherClients) {
    expansion_delay_ = std::chrono::milliseconds(300);
    start(/*build=*/true);
    auto client_a = connect();
    client_a->write(net::buffer(std::string(R"({"query": "alpha"})")));
    beast::error_code ignored;
    client_a->next_layer().close(ignored);
    client_a.reset();

    auto client_b = connect();
    client_b->write(net::buffer(std::string(R"({"query": "beta"})")));
    const auto expansion = read_message(*client_b);
    EXPECT_EQ(stage_of(expansion), "query_expansion");
    EXPECT_EQ(expansion.at("data").as_object().at("original").as_string(), "beta");
    EXPECT_EQ(stage_of(read_message(*client_b)), "keyword_search");
    EXPECT_EQ(stage_of(read_message(*client_b)), "done");
}

// BDD-CON-02 / NFR-007: повільна Ollama у WS-сесії не блокує REST-пошук іншого клієнта.
// Затримка D = 1 с; REST має завершитися менш ніж за D/2.
TEST_F(WsSearchTest, SlowExpansionDoesNotBlockRestSearch) {
    expansion_delay_ = std::chrono::milliseconds(1000);
    start(/*build=*/true, /*use_pool=*/true);
    auto slow = connect();
    slow->write(net::buffer(std::string(R"({"query": "alpha"})")));

    const auto started = std::chrono::steady_clock::now();
    const auto [status, body] = post_json("/api/search", R"({"query": "beta", "mode": "keyword"})");
    const auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_EQ(status, 200u);
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));
    // Повільний пайплайн усе ще завершується своїми подіями.
    EXPECT_EQ(stage_of(read_message(*slow)), "query_expansion");
    EXPECT_EQ(stage_of(read_message(*slow)), "keyword_search");
    EXPECT_EQ(stage_of(read_message(*slow)), "done");
}

// BDD-CON-05 / TEST-CONC-04: 8 одночасних WS-сесій; кожна отримує рівно три події і лише
// власний запит.
TEST_F(WsSearchTest, EightConcurrentSessionsAreIsolated) {
    start(/*build=*/true, /*use_pool=*/true);
    const std::vector<std::string> queries = {"alpha", "beta",  "gamma",      "delta",
                                              "here",  "words", "alpha beta", "gamma delta"};
    std::vector<std::vector<std::string>> stages(queries.size());
    std::vector<std::string> originals(queries.size());
    std::vector<std::thread> clients;
    for (std::size_t i = 0; i < queries.size(); ++i) {
        clients.emplace_back([this, i, &queries, &stages, &originals] {
            net::io_context ioc;
            websocket::stream<tcp::socket> ws(ioc);
            ws.next_layer().connect({net::ip::make_address("127.0.0.1"), port_});
            ws.handshake("127.0.0.1", "/ws/search");
            ws.write(net::buffer(std::string(R"({"query": ")") + queries[i] + R"("})"));
            for (int n = 0; n < 3; ++n) {
                const auto message = read_message(ws);
                stages[i].push_back(stage_of(message));
                if (stage_of(message) == "query_expansion") {
                    originals[i] =
                        std::string(message.at("data").as_object().at("original").as_string());
                }
            }
        });
    }
    for (std::thread &client : clients) {
        client.join();
    }
    for (std::size_t i = 0; i < queries.size(); ++i) {
        EXPECT_EQ(stages[i],
                  (std::vector<std::string>{"query_expansion", "keyword_search", "done"}))
            << "сесія " << i;
        EXPECT_EQ(originals[i], queries[i]) << "сесія " << i;
    }
}

// BDD-CON-06 / TEST-CONC-06: 4 сесії з затримкою Ollama 1 с завершуються паралельно (менш ніж за
// 4 с від початку), а не по черзі.
TEST_F(WsSearchTest, ParallelPipelinesFinishWithinSerialBudget) {
    expansion_delay_ = std::chrono::milliseconds(1000);
    start(/*build=*/true, /*use_pool=*/true);
    std::vector<char> finished(4, 0); // не vector<bool>: біти сусідніх елементів ділять слово
    std::vector<std::thread> clients;
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < finished.size(); ++i) {
        clients.emplace_back([this, i, &finished] {
            net::io_context ioc;
            websocket::stream<tcp::socket> ws(ioc);
            ws.next_layer().connect({net::ip::make_address("127.0.0.1"), port_});
            ws.handshake("127.0.0.1", "/ws/search");
            ws.write(net::buffer(std::string(R"({"query": "alpha"})")));
            for (int n = 0; n < 3; ++n) {
                read_message(ws);
            }
            finished[i] = 1;
        });
    }
    for (std::thread &client : clients) {
        client.join();
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    for (std::size_t i = 0; i < finished.size(); ++i) {
        EXPECT_TRUE(finished[i]) << "сесія " << i;
    }
    EXPECT_LT(elapsed, std::chrono::seconds(4));
}
