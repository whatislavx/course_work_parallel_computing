#include "ollama/endpoint.hpp"
#include "ollama/probe.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using search::ollama::Endpoint;
using search::ollama::models_contain;
using search::ollama::parse_ollama_url;

TEST(OllamaUrl, ParsesHostAndPort) {
    const auto ep = parse_ollama_url("http://ollama:11434", "m");
    ASSERT_TRUE(ep.has_value());
    EXPECT_EQ(ep->host, "ollama");
    EXPECT_EQ(ep->port, "11434");
    EXPECT_EQ(ep->model, "m");
}

TEST(OllamaUrl, DefaultsPortAndAcceptsTrailingSlash) {
    const auto plain = parse_ollama_url("http://localhost", "");
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(plain->port, "80");
    EXPECT_TRUE(parse_ollama_url("http://h:8080/", "").has_value());
}

TEST(OllamaUrl, RejectsInvalidValues) {
    EXPECT_FALSE(parse_ollama_url("https://x:1", "").has_value());
    EXPECT_FALSE(parse_ollama_url("abc", "").has_value());
    EXPECT_FALSE(parse_ollama_url("http://", "").has_value());
    EXPECT_FALSE(parse_ollama_url("http://:80", "").has_value());
    EXPECT_FALSE(parse_ollama_url("http://h:0", "").has_value());
    EXPECT_FALSE(parse_ollama_url("http://h:70000", "").has_value());
    EXPECT_FALSE(parse_ollama_url("http://h:port", "").has_value());
    EXPECT_FALSE(parse_ollama_url("http://h/api", "").has_value());
}

TEST(OllamaModels, FindsExactAndLatestTags) {
    const std::string body = R"({"models":[{"name":"qwen2.5:0.5b"},{"name":"tiny:latest"}]})";
    EXPECT_TRUE(models_contain(body, "qwen2.5:0.5b"));
    EXPECT_TRUE(models_contain(body, "tiny"));
    EXPECT_FALSE(models_contain(body, "qwen2.5"));
    EXPECT_FALSE(models_contain(body, "absent"));
}

TEST(OllamaModels, MalformedBodyIsNotAModel) {
    EXPECT_FALSE(models_contain("not json", "m"));
    EXPECT_FALSE(models_contain(R"({"models":"x"})", "m"));
    EXPECT_FALSE(models_contain(R"({})", "m"));
}

// Фейковий Ollama: приймає одне з'єднання, відповідає заданим рядком і закривається.
class FakeOllama {
  public:
    explicit FakeOllama(std::string response) : acceptor_(ioc_, tcp::endpoint(tcp::v4(), 0)) {
        thread_ = std::thread([this, response = std::move(response)] {
            tcp::socket socket(ioc_);
            acceptor_.accept(socket);
            beast::flat_buffer buffer;
            http::request<http::empty_body> req;
            http::read(socket, buffer, req);
            net::write(socket, net::buffer(response));
            beast::error_code ignored;
            socket.shutdown(tcp::socket::shutdown_both, ignored);
        });
    }
    ~FakeOllama() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    std::string port() const { return std::to_string(acceptor_.local_endpoint().port()); }

  private:
    net::io_context ioc_;
    tcp::acceptor acceptor_;
    std::thread thread_;
};

std::string http_200(const std::string &body) {
    return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

bool run_probe(const Endpoint &ep, std::chrono::milliseconds timeout) {
    net::io_context ioc;
    bool result = true;
    bool called = false;
    search::ollama::probe(ioc, ep, timeout, [&](bool ok) {
        result = ok;
        called = true;
    });
    ioc.run();
    EXPECT_TRUE(called);
    return result;
}

TEST(OllamaProbe, AvailableWhenModelPresent) {
    FakeOllama fake(http_200(R"({"models":[{"name":"m:latest"}]})"));
    EXPECT_TRUE(run_probe(Endpoint{"127.0.0.1", fake.port(), "m"}, std::chrono::seconds(5)));
}

TEST(OllamaProbe, UnavailableWhenModelMissing) {
    FakeOllama fake(http_200(R"({"models":[]})"));
    EXPECT_FALSE(run_probe(Endpoint{"127.0.0.1", fake.port(), "m"}, std::chrono::seconds(5)));
}

TEST(OllamaProbe, UnavailableWhenNothingListens) {
    // Порт, який одразу звільнили: з'єднання відхиляється.
    std::string port;
    {
        net::io_context ioc;
        tcp::acceptor tmp(ioc, tcp::endpoint(tcp::v4(), 0));
        port = std::to_string(tmp.local_endpoint().port());
    }
    EXPECT_FALSE(run_probe(Endpoint{"127.0.0.1", port, ""}, std::chrono::seconds(5)));
}

TEST(OllamaProbe, TimesOutOnSilentServer) {
    net::io_context ioc;
    tcp::acceptor silent(ioc, tcp::endpoint(tcp::v4(), 0));
    const auto port = std::to_string(silent.local_endpoint().port());
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(run_probe(Endpoint{"127.0.0.1", port, ""}, std::chrono::milliseconds(200)));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(3));
}

} // namespace
