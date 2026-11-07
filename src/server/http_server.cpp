#include "server/http_server.hpp"

#include "server/router.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <utility>

namespace search::server {
namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// Скільки байтів після відповіді вичитуємо й відкидаємо, перш ніж закрити з'єднання.
constexpr std::size_t kMaxDrainBytes = 1 << 20;

class Session : public std::enable_shared_from_this<Session> {
  public:
    Session(tcp::socket socket, const ServerOptions &options)
        : stream_(std::move(socket)), max_body_bytes_(options.max_request_body_bytes),
          timeout_(options.request_timeout) {}

    void start() { read_header(); }

  private:
    void read_header() {
        stream_.expires_after(timeout_);
        http::async_read_header(stream_, buffer_, parser_,
                                [self = shared_from_this()](beast::error_code ec, std::size_t) {
                                    self->on_header(ec);
                                });
    }

    // Порядок 06 §2.5: спершу шлях і метод, потім розмір тіла, потім обробка.
    void on_header(beast::error_code ec) {
        if (ec == http::error::end_of_stream || ec == beast::error::timeout) {
            close();
            return;
        }
        if (ec) {
            send(
                error_response(http::status::bad_request, "bad_request", "malformed HTTP request"));
            return;
        }

        dispatch();
    }

    void dispatch() {
        Response response = route(parser_.get());
        if (response.result() != http::status::not_implemented) {
            // 200, 404, 405: тіло запиту не читаємо.
            send(std::move(response));
            return;
        }

        // Маршрут прийнято (заглушка 501): перевіряємо розмір тіла.
        const auto length = parser_.content_length();
        if (length && *length > max_body_bytes_) {
            send(payload_too_large());
            return;
        }
        // Ліміт ставимо лише тут: Beast відхиляє тіло вже на етапі заголовків, тож раніше
        // запит на 404/405/health з великим тілом дав би 400 замість маршрутизації.
        parser_.body_limit(max_body_bytes_);
        stream_.expires_after(timeout_);
        http::async_read(stream_, buffer_, parser_,
                         [self = shared_from_this()](beast::error_code body_ec, std::size_t) {
                             self->on_body(body_ec);
                         });
    }

    void on_body(beast::error_code ec) {
        if (ec == http::error::body_limit) {
            send(payload_too_large());
            return;
        }
        if (ec) {
            close();
            return;
        }
        send(route(parser_.get()));
    }

    static Response payload_too_large() {
        return error_response(http::status::payload_too_large, "payload_too_large",
                              "request body exceeds the configured limit");
    }

    void send(Response response) {
        response.keep_alive(false);
        response.prepare_payload();
        auto msg = std::make_shared<Response>(std::move(response));
        stream_.expires_after(timeout_);
        http::async_write(stream_, *msg,
                          [self = shared_from_this(), msg](beast::error_code, std::size_t) {
                              beast::error_code ignored;
                              self->stream_.socket().shutdown(tcp::socket::shutdown_send, ignored);
                              self->drain();
                          });
    }

    // Lingering close: вичитуємо залишок тіла, щоб клієнт не отримав RST до читання відповіді.
    void drain() {
        if (drained_ >= kMaxDrainBytes) {
            close();
            return;
        }
        stream_.expires_after(timeout_);
        stream_.async_read_some(net::buffer(scratch_),
                                [self = shared_from_this()](beast::error_code ec, std::size_t n) {
                                    if (ec) {
                                        self->close();
                                        return;
                                    }
                                    self->drained_ += n;
                                    self->drain();
                                });
    }

    void close() {
        beast::error_code ignored;
        stream_.socket().shutdown(tcp::socket::shutdown_both, ignored);
        stream_.socket().close(ignored);
    }

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request_parser<http::string_body> parser_;
    std::size_t max_body_bytes_;
    std::chrono::milliseconds timeout_;
    std::array<char, 4096> scratch_{};
    std::size_t drained_ = 0;
};

} // namespace

HttpServer::HttpServer(net::io_context &ioc, const ServerOptions &options)
    : options_(options), acceptor_(ioc), retry_timer_(ioc) {
    const tcp::endpoint endpoint(tcp::v4(), options.port);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(net::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(net::socket_base::max_listen_connections);
}

void HttpServer::start() { do_accept(); }

// Викликати лише з потоку io_context (див. заголовок).
void HttpServer::stop() {
    beast::error_code ignored;
    acceptor_.close(ignored);
    retry_timer_.cancel(ignored);
}

std::uint16_t HttpServer::port() const { return acceptor_.local_endpoint().port(); }

void HttpServer::do_accept() {
    acceptor_.async_accept([this](beast::error_code ec, tcp::socket socket) {
        if (ec == net::error::operation_aborted) {
            return;
        }
        if (!ec) {
            std::make_shared<Session>(std::move(socket), options_)->start();
            do_accept();
            return;
        }
        // Помилка accept (наприклад EMFILE): логуємо і повторюємо з паузою, а не в циклі.
        std::cerr << "accept failed: " << ec.message() << '\n';
        retry_timer_.expires_after(std::chrono::milliseconds(100));
        retry_timer_.async_wait([this](beast::error_code wait_ec) {
            if (!wait_ec && acceptor_.is_open()) {
                do_accept();
            }
        });
    });
}

} // namespace search::server
