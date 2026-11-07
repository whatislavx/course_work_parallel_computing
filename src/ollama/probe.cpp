#include "ollama/probe.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace search::ollama {
namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// Відповідь /api/tags невелика; обмежуємо, щоб некоректний сервіс не виснажив пам'ять.
constexpr std::size_t kMaxTagsBytes = 1 << 20;

class Probe : public std::enable_shared_from_this<Probe> {
  public:
    Probe(net::io_context &ioc, Endpoint endpoint, ProbeCallback done)
        : endpoint_(std::move(endpoint)), done_(std::move(done)), resolver_(ioc), socket_(ioc),
          deadline_(ioc) {}

    void run(std::chrono::milliseconds timeout) {
        deadline_.expires_after(timeout);
        deadline_.async_wait([self = shared_from_this()](beast::error_code ec) {
            if (!ec) {
                self->finish(false);
            }
        });
        resolver_.async_resolve(
            endpoint_.host, endpoint_.port,
            [self = shared_from_this()](beast::error_code ec, tcp::resolver::results_type results) {
                if (self->finished_) {
                    return;
                }
                if (ec) {
                    self->finish(false);
                    return;
                }
                self->connect(results);
            });
    }

  private:
    void connect(const tcp::resolver::results_type &results) {
        net::async_connect(
            socket_, results,
            [self = shared_from_this()](beast::error_code ec, const tcp::endpoint &) {
                if (ec) {
                    self->finish(false);
                    return;
                }
                self->send_request();
            });
    }

    void send_request() {
        request_.version(11);
        request_.method(http::verb::get);
        request_.target("/api/tags");
        request_.set(http::field::host, endpoint_.host + ":" + endpoint_.port);
        request_.set(http::field::connection, "close");
        http::async_write(socket_, request_,
                          [self = shared_from_this()](beast::error_code ec, std::size_t) {
                              if (ec) {
                                  self->finish(false);
                                  return;
                              }
                              self->read_response();
                          });
    }

    void read_response() {
        parser_.body_limit(kMaxTagsBytes);
        http::async_read(socket_, buffer_, parser_,
                         [self = shared_from_this()](beast::error_code ec, std::size_t) {
                             if (ec) {
                                 self->finish(false);
                                 return;
                             }
                             const auto &res = self->parser_.get();
                             const bool ok = res.result() == http::status::ok &&
                                             (self->endpoint_.model.empty() ||
                                              models_contain(res.body(), self->endpoint_.model));
                             self->finish(ok);
                         });
    }

    void finish(bool available) {
        if (finished_) {
            return;
        }
        finished_ = true;
        beast::error_code ignored;
        deadline_.cancel(ignored);
        resolver_.cancel();
        socket_.shutdown(tcp::socket::shutdown_both, ignored);
        socket_.close(ignored);
        done_(available);
    }

    Endpoint endpoint_;
    ProbeCallback done_;
    tcp::resolver resolver_;
    tcp::socket socket_;
    net::steady_timer deadline_;
    http::request<http::empty_body> request_;
    beast::flat_buffer buffer_;
    http::response_parser<http::string_body> parser_;
    bool finished_ = false;
};

} // namespace

void probe(net::io_context &ioc, Endpoint endpoint, std::chrono::milliseconds timeout,
           ProbeCallback done) {
    std::make_shared<Probe>(ioc, std::move(endpoint), std::move(done))->run(timeout);
}

bool models_contain(std::string_view tags_json, std::string_view model) {
    boost::system::error_code ec;
    const auto value = boost::json::parse(tags_json, ec);
    if (ec || !value.is_object()) {
        return false;
    }
    const auto *models = value.as_object().if_contains("models");
    if (models == nullptr || !models->is_array()) {
        return false;
    }
    const std::string latest = std::string(model) + ":latest";
    for (const auto &entry : models->as_array()) {
        if (!entry.is_object()) {
            continue;
        }
        const auto *name = entry.as_object().if_contains("name");
        if (name == nullptr || !name->is_string()) {
            continue;
        }
        const std::string_view n = name->as_string();
        if (n == model || n == latest) {
            return true;
        }
    }
    return false;
}

} // namespace search::ollama
