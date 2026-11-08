// Контрактні тести EP-02 (POST /api/index) з відкладеною відповіддю (TCK-013).
#include "index/index_manager.hpp"
#include "server/http_server.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using search::index::BuildCallback;
using search::index::BuildResult;
using search::server::HttpServer;
using search::server::IndexStarter;
using search::server::ServerOptions;

constexpr std::size_t kBodyLimit = 64;
constexpr std::size_t kDefaultThreads = 2;
constexpr std::size_t kMaxThreads = 8;

// Створює тимчасовий каталог з трьома текстовими документами.
std::filesystem::path make_dataset(const std::string &name) {
    const auto dir = std::filesystem::temp_directory_path() / ("index_endpoint_" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    for (int i = 0; i < 3; ++i) {
        std::ofstream out(dir / ("d" + std::to_string(i) + ".txt"));
        out << "alpha beta gamma " << i << '\n';
    }
    return dir;
}

class IndexEndpointTest : public ::testing::Test {
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

    // Піднімає сервер з заданим стартером побудови; потік io_context крутиться у фоні.
    void start_server(IndexStarter starter) {
        ServerOptions options;
        options.port = 0;
        options.max_request_body_bytes = kBodyLimit;
        options.request_timeout = std::chrono::seconds(5);
        options.index_start = std::move(starter);
        options.default_index_threads = kDefaultThreads;
        options.max_index_threads = kMaxThreads;
        options.stats_provider = [this] { return manager_.stats(); };
        server_ = std::make_unique<HttpServer>(ioc_, options);
        server_->start();
        port_ = server_->port();
        worker_ = std::thread([this] { ioc_.run(); });
    }

    // Стартер з реальним IndexManager над датасетом dataset.
    IndexStarter real_starter(const std::filesystem::path &dataset) {
        return [this, dataset](std::size_t threads, BuildCallback done) {
            return manager_.start_build(dataset, threads, std::move(done));
        };
    }

    http::response<http::string_body> request(const std::string &body,
                                              http::verb verb = http::verb::post,
                                              const std::string &target = "/api/index") {
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

    std::filesystem::path dataset_;
    search::index::IndexManager manager_;
    net::io_context ioc_;
    std::unique_ptr<HttpServer> server_;
    std::thread worker_;
    std::uint16_t port_ = 0;
};

} // namespace

// Порожнє тіло → DEFAULT_INDEX_THREADS; відповідь містить лише поля контракту.
TEST_F(IndexEndpointTest, EmptyBodyBuildsWithDefaultThreadsAndReturnsStats) {
    dataset_ = make_dataset("empty_body");
    std::atomic<std::size_t> seen_threads{0};
    start_server([this, &seen_threads](std::size_t threads, BuildCallback done) {
        seen_threads = threads;
        return manager_.start_build(dataset_, threads, std::move(done));
    });

    const auto res = request("");
    ASSERT_EQ(res.result(), http::status::ok);
    EXPECT_EQ(seen_threads.load(), kDefaultThreads);
    const auto body = boost::json::parse(res.body()).as_object();
    EXPECT_EQ(body.at("status").as_string(), "ok");
    EXPECT_EQ(body.at("documents_indexed").as_int64(), 3);
    EXPECT_GE(body.at("time_ms").as_int64(), 0);
    EXPECT_EQ(body.size(), 3u);
}

// Явний threads передається в побудову.
TEST_F(IndexEndpointTest, ExplicitThreadsAreUsed) {
    dataset_ = make_dataset("explicit");
    std::atomic<std::size_t> seen_threads{0};
    start_server([this, &seen_threads](std::size_t threads, BuildCallback done) {
        seen_threads = threads;
        return manager_.start_build(dataset_, threads, std::move(done));
    });

    const auto res = request(R"({"threads": 4})");
    ASSERT_EQ(res.result(), http::status::ok);
    EXPECT_EQ(seen_threads.load(), 4u);
}

// Вихід за межі 1…MAX_INDEX_THREADS і нецілі значення → 400 invalid_threads.
TEST_F(IndexEndpointTest, OutOfRangeOrNonIntegerThreadsReturnInvalidThreads) {
    start_server([](std::size_t, BuildCallback) {
        ADD_FAILURE() << "побудова не має стартувати для некоректного threads";
        return false;
    });
    for (const char *body :
         {R"({"threads": 0})", R"({"threads": 9})", R"({"threads": -1})", R"({"threads": 2.5})",
          R"({"threads": "4"})", R"({"threads": null})"}) {
        const auto res = request(body);
        EXPECT_EQ(res.result(), http::status::bad_request) << body;
        const auto error = boost::json::parse(res.body()).as_object().at("error").as_object();
        EXPECT_EQ(error.at("code").as_string(), "invalid_threads") << body;
    }
}

// Тіло, що не є JSON-об'єктом → 400 invalid_json.
TEST_F(IndexEndpointTest, MalformedJsonReturnsInvalidJson) {
    start_server([](std::size_t, BuildCallback) {
        ADD_FAILURE() << "побудова не має стартувати для некоректного JSON";
        return false;
    });
    for (const char *body : {"{not json", "[1,2]"}) {
        const auto res = request(body);
        EXPECT_EQ(res.result(), http::status::bad_request) << body;
        const auto error = boost::json::parse(res.body()).as_object().at("error").as_object();
        EXPECT_EQ(error.at("code").as_string(), "invalid_json") << body;
    }
}

// Побудова вже триває → 409 index_build_in_progress.
TEST_F(IndexEndpointTest, SecondBuildWhileRunningReturnsConflict) {
    start_server([](std::size_t, BuildCallback) { return false; });

    const auto res = request(R"({"threads": 1})");
    EXPECT_EQ(res.result(), http::status::conflict);
    const auto error = boost::json::parse(res.body()).as_object().at("error").as_object();
    EXPECT_EQ(error.at("code").as_string(), "index_build_in_progress");
}

// Відсутній каталог датасету → 500 dataset_unavailable.
TEST_F(IndexEndpointTest, MissingDatasetReturnsDatasetUnavailable) {
    const auto missing = std::filesystem::temp_directory_path() / "index_endpoint_missing_dir";
    std::filesystem::remove_all(missing);
    start_server(real_starter(missing));

    const auto res = request(R"({"threads": 1})");
    ASSERT_EQ(res.result(), http::status::internal_server_error);
    const auto error = boost::json::parse(res.body()).as_object().at("error").as_object();
    EXPECT_EQ(error.at("code").as_string(), "dataset_unavailable");
}

// Тіло над лімітом → 413 payload_too_large.
TEST_F(IndexEndpointTest, OversizedBodyReturnsPayloadTooLarge) {
    start_server([](std::size_t, BuildCallback) {
        ADD_FAILURE() << "побудова не має стартувати для завеликого тіла";
        return false;
    });
    const std::string body =
        R"({"threads": 1, "padding": ")" + std::string(kBodyLimit, 'x') + R"("})";
    const auto res = request(body);
    EXPECT_EQ(res.result(), http::status::payload_too_large);
}

// Відповідь відкладається до завершення побудови: клієнт отримує її лише після callback.
TEST_F(IndexEndpointTest, ResponseIsDeferredUntilBuildCompletes) {
    std::mutex mutex;
    BuildCallback pending;
    std::atomic<bool> started{false};
    start_server([&](std::size_t, BuildCallback done) {
        std::lock_guard lock(mutex);
        pending = std::move(done);
        started = true;
        return true;
    });

    std::thread finisher([&] {
        while (!started.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        BuildResult result;
        result.ok = true;
        result.stats.documents = 7;
        result.stats.index_build_time_ms = 42;
        result.stats.threads_used = 1;
        BuildCallback done;
        {
            std::lock_guard lock(mutex);
            done = pending;
        }
        done(result);
    });

    const auto start = std::chrono::steady_clock::now();
    const auto res = request(R"({"threads": 1})");
    const auto waited = std::chrono::steady_clock::now() - start;
    finisher.join();

    ASSERT_EQ(res.result(), http::status::ok);
    EXPECT_GE(waited, std::chrono::milliseconds(120));
    const auto body = boost::json::parse(res.body()).as_object();
    EXPECT_EQ(body.at("documents_indexed").as_int64(), 7);
    EXPECT_EQ(body.at("time_ms").as_int64(), 42);
}

// Збій побудови з callback → 500 з кодом помилки.
TEST_F(IndexEndpointTest, FailedBuildCallbackReturnsServerError) {
    std::mutex mutex;
    BuildCallback pending;
    std::atomic<bool> started{false};
    start_server([&](std::size_t, BuildCallback done) {
        std::lock_guard lock(mutex);
        pending = std::move(done);
        started = true;
        return true;
    });
    std::thread finisher([&] {
        while (!started.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        BuildResult result;
        result.error_code = "index_build_failed";
        result.error_message = "тестовий збій";
        BuildCallback done;
        {
            std::lock_guard lock(mutex);
            done = pending;
        }
        done(result);
    });

    const auto res = request(R"({"threads": 1})");
    finisher.join();
    ASSERT_EQ(res.result(), http::status::internal_server_error);
    const auto error = boost::json::parse(res.body()).as_object().at("error").as_object();
    EXPECT_EQ(error.at("code").as_string(), "index_build_failed");
}

// Метод GET на /api/index → 405 з Allow: POST (контракт маршрутизації не змінився).
TEST_F(IndexEndpointTest, GetOnIndexReturnsMethodNotAllowed) {
    start_server([](std::size_t, BuildCallback) { return false; });
    const auto res = request("", http::verb::get, "/api/index");
    EXPECT_EQ(res.result(), http::status::method_not_allowed);
    EXPECT_EQ(res[http::field::allow], "POST");
}

// Регресія рев'ю: Content-Length понад 1 MiB (дефолтний ліміт Beast) має давати 413, а не 400.
TEST_F(IndexEndpointTest, ContentLengthAboveBeastDefaultReturnsPayloadTooLarge) {
    start_server([](std::size_t, BuildCallback) {
        ADD_FAILURE() << "побудова не має стартувати для завеликого тіла";
        return false;
    });
    net::io_context client_ioc;
    tcp::socket socket(client_ioc);
    socket.connect({net::ip::make_address("127.0.0.1"), port_});
    const std::string header = "POST /api/index HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                               "Content-Type: application/json\r\nContent-Length: 2097152\r\n\r\n";
    net::write(socket, net::buffer(header));
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(socket, buffer, res);
    EXPECT_EQ(res.result(), http::status::payload_too_large);
}

// EP-04: до першої побудови — нулі (FR-011).
TEST_F(IndexEndpointTest, StatsBeforeBuildReturnsZeros) {
    start_server([](std::size_t, BuildCallback) { return false; });
    const auto res = request("", http::verb::get, "/api/stats");
    ASSERT_EQ(res.result(), http::status::ok);
    const auto body = boost::json::parse(res.body()).as_object();
    EXPECT_EQ(body.at("documents").as_int64(), 0);
    EXPECT_EQ(body.at("index_build_time_ms").as_int64(), 0);
    EXPECT_EQ(body.at("threads_used").as_int64(), 0);
    EXPECT_EQ(body.size(), 3u);
}

// EP-04 після побудови повертає статистику активного індексу.
TEST_F(IndexEndpointTest, StatsAfterBuildReflectActiveIndex) {
    dataset_ = make_dataset("stats_after_build");
    start_server(real_starter(dataset_));
    ASSERT_EQ(request(R"({"threads": 3})").result(), http::status::ok);

    const auto res = request("", http::verb::get, "/api/stats");
    ASSERT_EQ(res.result(), http::status::ok);
    const auto body = boost::json::parse(res.body()).as_object();
    EXPECT_EQ(body.at("documents").as_int64(), 3);
    EXPECT_EQ(body.at("threads_used").as_int64(), 3);
    EXPECT_GE(body.at("index_build_time_ms").as_int64(), 0);
}

// Метод POST на /api/stats → 405 з Allow: GET.
TEST_F(IndexEndpointTest, PostOnStatsReturnsMethodNotAllowed) {
    start_server([](std::size_t, BuildCallback) { return false; });
    const auto res = request("", http::verb::post, "/api/stats");
    EXPECT_EQ(res.result(), http::status::method_not_allowed);
    EXPECT_EQ(res[http::field::allow], "GET");
}
