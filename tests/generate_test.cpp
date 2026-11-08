// Тести розширення запиту через Ollama (TCK-020): промпт, розбір, клієнт з тайм-аутом.
#include "ollama/generate.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using search::ollama::Endpoint;
using search::ollama::generate;
using search::ollama::parse_expansion_terms;
using search::ollama::parse_generate_response;

// Фейковий Ollama: відповідає заданим тілом або мовчить (тайм-аут).
class FakeOllama {
  public:
    explicit FakeOllama(std::string reply_body, bool silent = false)
        : acceptor_(ioc_, tcp::endpoint(tcp::v4(), 0)) {
        port_ = acceptor_.local_endpoint().port();
        thread_ = std::thread([this, reply_body, silent] {
            tcp::socket socket(ioc_);
            acceptor_.accept(socket);
            beast::flat_buffer buffer;
            http::request<http::string_body> req;
            http::read(socket, buffer, req);
            if (silent) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                return;
            }
            http::response<http::string_body> res{http::status::ok, 11};
            res.set(http::field::content_type, "application/json");
            res.body() = reply_body;
            res.prepare_payload();
            http::write(socket, res);
        });
    }
    ~FakeOllama() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    std::string port() const { return std::to_string(port_); }

  private:
    net::io_context ioc_;
    tcp::acceptor acceptor_;
    unsigned short port_ = 0;
    std::thread thread_;
};

Endpoint endpoint_for(const std::string &port) { return Endpoint{"127.0.0.1", port, "tinyllama"}; }

} // namespace

TEST(ExpansionPrompt, ContainsQueryAndTermLimit) {
    const std::string prompt = search::ollama::build_expansion_prompt("parallel search", 5);
    EXPECT_NE(prompt.find("parallel search"), std::string::npos);
    EXPECT_NE(prompt.find("5"), std::string::npos);
}

TEST(ExpansionResponse, ExtractsResponseField) {
    EXPECT_EQ(parse_generate_response(R"({"response": "gamma, delta", "done": true})"),
              std::optional<std::string>("gamma, delta"));
}

TEST(ExpansionResponse, MalformedOrMissingFieldIsRejected) {
    EXPECT_FALSE(parse_generate_response("{not json"));
    EXPECT_FALSE(parse_generate_response(R"({"other": "x"})"));
    EXPECT_FALSE(parse_generate_response(R"({"response": 7})"));
    EXPECT_FALSE(parse_generate_response("[1]"));
}

TEST(ExpansionTerms, ExcludesQueryTermsDuplicatesAndCapsCount) {
    const auto terms =
        parse_expansion_terms("Alpha, gamma, delta, GAMMA, epsilon, zeta", {"alpha"}, 3);
    EXPECT_EQ(terms, (std::vector<std::string>{"gamma", "delta", "epsilon"}));
}

TEST(ExpansionTerms, EmptyModelTextGivesNoTerms) {
    EXPECT_TRUE(parse_expansion_terms("", {"alpha"}, 5).empty());
}

TEST(ExpansionClient, SuccessReturnsResponseText) {
    FakeOllama fake(R"({"response": "gamma, delta", "done": true})");
    net::io_context ioc;
    std::optional<std::string> result;
    bool called = false;
    generate(ioc, endpoint_for(fake.port()), "prompt", std::chrono::seconds(5),
             [&](std::optional<std::string> response) {
                 called = true;
                 result = std::move(response);
             });
    ioc.run();
    EXPECT_TRUE(called);
    EXPECT_EQ(result, std::optional<std::string>("gamma, delta"));
}

TEST(ExpansionClient, TimeoutCallsBackWithNothing) {
    FakeOllama fake("", /*silent=*/true);
    net::io_context ioc;
    bool called = false;
    std::optional<std::string> result = "unset";
    generate(ioc, endpoint_for(fake.port()), "prompt", std::chrono::milliseconds(200),
             [&](std::optional<std::string> response) {
                 called = true;
                 result = std::move(response);
             });
    ioc.run();
    EXPECT_TRUE(called);
    EXPECT_FALSE(result.has_value());
}

TEST(ExpansionClient, UnreachableServiceCallsBackWithNothing) {
    net::io_context ioc;
    bool called = false;
    std::optional<std::string> result = "unset";
    // Порт 1 на loopback зазвичай закритий: з'єднання відхиляється.
    generate(ioc, Endpoint{"127.0.0.1", "1", "tinyllama"}, "prompt", std::chrono::seconds(5),
             [&](std::optional<std::string> response) {
                 called = true;
                 result = std::move(response);
             });
    ioc.run();
    EXPECT_TRUE(called);
    EXPECT_FALSE(result.has_value());
}
