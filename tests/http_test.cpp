#include "server/http_server.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using search::server::HttpServer;
using search::server::ServerOptions;

constexpr std::size_t kLimit = 64;

class HttpServerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ServerOptions options;
        options.port = 0;
        options.max_request_body_bytes = kLimit;
        options.request_timeout = std::chrono::seconds(5);
        options.ollama_check = [this](std::function<void(bool)> done) { done(ollama_ok_.load()); };
        server_ = std::make_unique<HttpServer>(ioc_, options);
        server_->start();
        worker_ = std::thread([this] { ioc_.run(); });
    }

    void TearDown() override {
        // stop() вызываємо в потоці io_context, як того вимагає заголовок.
        net::post(ioc_, [this] {
            server_->stop();
            ioc_.stop();
        });
        worker_.join();
    }

    http::response<http::string_body> request(http::verb verb, const std::string &target,
                                              const std::string &body = "") {
        net::io_context client_ioc;
        tcp::socket socket(client_ioc);
        socket.connect({net::ip::make_address("127.0.0.1"), server_->port()});

        http::request<http::string_body> req{verb, target, 11};
        req.set(http::field::host, "127.0.0.1");
        req.body() = body;
        req.prepare_payload();
        http::write(socket, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(socket, buffer, res);
        return res;
    }

    std::atomic<bool> ollama_ok_{true};
    net::io_context ioc_;
    std::unique_ptr<HttpServer> server_;
    std::thread worker_;
};

} // namespace

TEST_F(HttpServerTest, HealthReturnsValidJsonWithStringStatus) {
    const auto res = request(http::verb::get, "/api/health");
    EXPECT_EQ(res.result(), http::status::ok);
    EXPECT_EQ(res.base()["Content-Type"], "application/json; charset=utf-8");
    const auto parsed = boost::json::parse(res.body());
    ASSERT_TRUE(parsed.is_object());
    EXPECT_EQ(parsed.as_object().at("status").as_string(), "ok");
}

TEST_F(HttpServerTest, HealthReportsOllamaAvailability) {
    const auto up = boost::json::parse(request(http::verb::get, "/api/health").body());
    EXPECT_TRUE(up.as_object().at("ollama").as_bool());

    ollama_ok_ = false;
    const auto down = boost::json::parse(request(http::verb::get, "/api/health").body());
    EXPECT_FALSE(down.as_object().at("ollama").as_bool());
    EXPECT_EQ(down.as_object().at("status").as_string(), "ok");
}

TEST_F(HttpServerTest, HealthWithQueryStringStillOk) {
    EXPECT_EQ(request(http::verb::get, "/api/health?x=1").result(), http::status::ok);
}

TEST_F(HttpServerTest, HealthIgnoresOversizedBody) {
    const std::string big(kLimit * 4, 'x');
    EXPECT_EQ(request(http::verb::get, "/api/health", big).result(), http::status::ok);
}

TEST_F(HttpServerTest, UnknownPathReturnsNotFoundInApiFormat) {
    const auto res = request(http::verb::get, "/api/unknown");
    EXPECT_EQ(res.result(), http::status::not_found);
    EXPECT_NE(res.body().find("\"code\":\"not_found\""), std::string::npos);
}

TEST_F(HttpServerTest, WrongMethodReturnsMethodNotAllowedWithAllowHeader) {
    const auto res = request(http::verb::delete_, "/api/index");
    EXPECT_EQ(res.result(), http::status::method_not_allowed);
    EXPECT_EQ(res.base()["Allow"], "POST");
    EXPECT_NE(res.body().find("\"code\":\"method_not_allowed\""), std::string::npos);
}

TEST_F(HttpServerTest, WrongMethodWithOversizedBodyStillReturns405) {
    const std::string big(kLimit * 4, 'x');
    EXPECT_EQ(request(http::verb::delete_, "/api/index", big).result(),
              http::status::method_not_allowed);
}

TEST_F(HttpServerTest, UnknownPathWithOversizedBodyStillReturns404) {
    const std::string big(kLimit * 4, 'x');
    EXPECT_EQ(request(http::verb::post, "/api/unknown", big).result(), http::status::not_found);
}

TEST_F(HttpServerTest, BodyAtLimitIsAccepted) {
    const std::string exact(kLimit, 'x');
    EXPECT_EQ(request(http::verb::post, "/api/search", exact).result(),
              http::status::not_implemented);
}

TEST_F(HttpServerTest, BodyOneByteOverLimitReturnsPayloadTooLarge) {
    const std::string over(kLimit + 1, 'x');
    const auto res = request(http::verb::post, "/api/search", over);
    EXPECT_EQ(res.result(), http::status::payload_too_large);
    EXPECT_NE(res.body().find("\"code\":\"payload_too_large\""), std::string::npos);
}

TEST_F(HttpServerTest, ServerStillHealthyAfterRejectedRequests) {
    const std::string over(kLimit + 1, 'x');
    request(http::verb::post, "/api/search", over);
    request(http::verb::get, "/api/unknown");
    EXPECT_EQ(request(http::verb::get, "/api/health").result(), http::status::ok);
}
