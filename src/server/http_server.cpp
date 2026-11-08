#include "server/http_server.hpp"

#include "query/search_service.hpp"
#include "server/router.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

#include <array>
#include <boost/none.hpp>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <utility>
#include <variant>

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
          timeout_(options.request_timeout), ollama_check_(options.ollama_check),
          index_start_(options.index_start), default_index_threads_(options.default_index_threads),
          max_index_threads_(options.max_index_threads), stats_provider_(options.stats_provider),
          search_handler_(options.search_handler), expansion_handler_(options.expansion_handler),
          max_query_length_(options.max_query_length) {}

    void start() { read_header(); }

  private:
    void read_header() {
        // Beast за замовчуванням відхиляє тіло понад 1 MiB ще на етапі заголовків (400).
        // Ліміт задає сесія: явна перевірка Content-Length дає 413 для будь-якого налаштування.
        parser_.body_limit(boost::none);
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
        if (ec == http::error::body_limit) {
            send(payload_too_large());
            return;
        }
        if (ec) {
            send(
                error_response(http::status::bad_request, "bad_request", "malformed HTTP request"));
            return;
        }

        if (is_health_request(parser_.get()) && ollama_check_) {
            ollama_check_(
                [self = shared_from_this()](bool available) { self->dispatch(available); });
            return;
        }
        dispatch(false);
    }

    void dispatch(bool ollama_available) {
        if (is_index_request(parser_.get())) {
            read_index_body();
            return;
        }
        if (is_search_request(parser_.get())) {
            read_request_body(
                [self = shared_from_this()](beast::error_code ec) { self->on_search_body(ec); });
            return;
        }
        if (is_stats_request(parser_.get())) {
            send(stats_response(stats_provider_ ? stats_provider_() : index::IndexStats{}));
            return;
        }
        Response response = route(parser_.get(), ollama_available);
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

    // EP-02: тіло читаємо з тим самим лімітом; відповідь відкладаємо до кінця побудови.
    void read_index_body() {
        read_request_body(
            [self = shared_from_this()](beast::error_code ec) { self->on_index_body(ec); });
    }

    // Читає тіло з лімітом max_body_bytes_ і передає результат у on_ready (413 — окремо).
    template <typename OnReady> void read_request_body(OnReady on_ready) {
        const auto length = parser_.content_length();
        if (length && *length > max_body_bytes_) {
            send(payload_too_large());
            return;
        }
        parser_.body_limit(max_body_bytes_);
        stream_.expires_after(timeout_);
        http::async_read(stream_, buffer_, parser_,
                         [on_ready = std::move(on_ready)](beast::error_code body_ec, std::size_t) {
                             on_ready(body_ec);
                         });
    }

    void on_index_body(beast::error_code ec) {
        if (ec == http::error::body_limit) {
            send(payload_too_large());
            return;
        }
        if (ec) {
            close();
            return;
        }
        auto parsed =
            parse_index_body(parser_.get().body(), default_index_threads_, max_index_threads_);
        if (std::holds_alternative<Response>(parsed)) {
            send(std::get<Response>(std::move(parsed)));
            return;
        }
        start_index_build(std::get<std::size_t>(parsed));
    }

    // Callback побудови викликається з робочого потоку менеджера. Відповідь пишеться лише
    // в потоці io_context: через net::post до виконавця сокета.
    void start_index_build(std::size_t threads) {
        if (!index_start_) {
            send(error_response(http::status::internal_server_error, "internal_error",
                                "index endpoint is not configured"));
            return;
        }
        bool started = false;
        try {
            started = index_start_(
                threads, [self = shared_from_this()](const index::BuildResult &result) {
                    net::post(self->stream_.get_executor(),
                              [self, result]() { self->finish_index_build(result); });
                });
        } catch (const std::exception &e) {
            // Подробиці (наприклад, системна помилка створення потоку) не віддаємо клієнту.
            std::cerr << "index build start failed: " << e.what() << '\n';
            send(error_response(http::status::internal_server_error, "internal_error",
                                "failed to start index build"));
            return;
        } catch (...) {
            send(error_response(http::status::internal_server_error, "internal_error",
                                "failed to start index build"));
            return;
        }
        if (!started) {
            send(error_response(http::status::conflict, "index_build_in_progress",
                                "index build is already running"));
        }
    }

    void finish_index_build(const index::BuildResult &result) {
        if (result.ok) {
            boost::json::object body{{"status", "ok"},
                                     {"documents_indexed", result.stats.documents},
                                     {"time_ms", result.stats.index_build_time_ms}};
            send(json_body(http::status::ok, boost::json::serialize(body)));
            return;
        }
        send(error_response(http::status::internal_server_error, result.error_code,
                            result.error_message));
    }

    static Response internal_error_response() {
        return error_response(http::status::internal_server_error, "internal_error",
                              "search failed");
    }

    // Пошук і відповідь 200. Винятки обробника ловимо тут: вони не виходять з io_context.
    // send викликається рівно один раз.
    void complete_search(const std::string &text, const std::vector<std::string> &extra_terms) {
        Response response = internal_error_response();
        try {
            if (!search_handler_) {
                response = error_response(http::status::internal_server_error, "internal_error",
                                          "search endpoint is not configured");
            } else {
                const auto started = std::chrono::steady_clock::now();
                std::vector<query::SearchHit> hits = search_handler_(text, extra_terms);
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started);
                response = search_response(hits, static_cast<std::uint64_t>(elapsed.count()));
            }
        } catch (const query::IndexNotReadyError &) {
            response =
                error_response(http::status::conflict, "index_not_ready", "index is not built yet");
        } catch (const std::exception &e) {
            std::cerr << "search failed: " << e.what() << '\n';
        } catch (...) {
            // Невідомий тип винятку: відповідь 500 вище.
        }
        send(std::move(response));
    }

    void on_search_body(beast::error_code ec) {
        if (ec == http::error::body_limit) {
            send(payload_too_large());
            return;
        }
        if (ec) {
            close();
            return;
        }
        auto parsed = parse_search_body(parser_.get().body(), max_query_length_);
        if (std::holds_alternative<Response>(parsed)) {
            send(std::get<Response>(std::move(parsed)));
            return;
        }
        const SearchRequest request = std::get<SearchRequest>(parsed);
        if (request.expanded && expansion_handler_) {
            // Розширення асинхронне: сесія чекає callback, io_context не блокується (FR-021:
            // збій або тайм-аут дає порожній список, тобто пошук без розширення).
            expansion_handler_(request.query, [self = shared_from_this(), text = request.query](
                                                  std::vector<std::string> terms) {
                self->complete_search(text, terms);
            });
            return;
        }
        complete_search(request.query, {});
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
        send(route(parser_.get(), false));
    }

    static Response json_body(http::status status, std::string body) {
        Response res{status, 11};
        res.set(http::field::server, "search-engine");
        res.set(http::field::content_type, "application/json; charset=utf-8");
        res.body() = std::move(body);
        return res;
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
    OllamaCheck ollama_check_;
    IndexStarter index_start_;
    std::size_t default_index_threads_;
    std::size_t max_index_threads_;
    std::function<index::IndexStats()> stats_provider_;
    SearchHandler search_handler_;
    ExpansionHandler expansion_handler_;
    std::size_t max_query_length_;
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
