#include "ollama/generate.hpp"

#include "index/tokenizer.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>

namespace search::ollama {
namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// Відповідь моделі обмежуємо, щоб некоректний сервіс не виснажив пам'ять.
constexpr std::size_t kMaxGenerateBytes = 1 << 20;

class Generate : public std::enable_shared_from_this<Generate> {
  public:
    Generate(net::io_context &ioc, Endpoint endpoint, std::string prompt, GenerateCallback done)
        : endpoint_(std::move(endpoint)), prompt_(std::move(prompt)), done_(std::move(done)),
          resolver_(ioc), socket_(ioc), deadline_(ioc) {}

    void run(std::chrono::milliseconds timeout) {
        deadline_.expires_after(timeout);
        deadline_.async_wait([self = shared_from_this()](beast::error_code ec) {
            if (!ec) {
                self->finish(std::nullopt);
            }
        });
        resolver_.async_resolve(
            endpoint_.host, endpoint_.port,
            [self = shared_from_this()](beast::error_code ec, tcp::resolver::results_type results) {
                if (self->finished_) {
                    return;
                }
                if (ec) {
                    self->finish(std::nullopt);
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
                    self->finish(std::nullopt);
                    return;
                }
                self->send_request();
            });
    }

    void send_request() {
        boost::json::object body{
            {"model", endpoint_.model}, {"prompt", prompt_}, {"stream", false},
            {"options", boost::json::object{{"temperature", 0}}}};
        request_.version(11);
        request_.method(http::verb::post);
        request_.target("/api/generate");
        request_.set(http::field::host, endpoint_.host + ":" + endpoint_.port);
        request_.set(http::field::content_type, "application/json");
        request_.set(http::field::connection, "close");
        request_.body() = boost::json::serialize(body);
        request_.prepare_payload();
        http::async_write(socket_, request_,
                          [self = shared_from_this()](beast::error_code ec, std::size_t) {
                              if (ec) {
                                  self->finish(std::nullopt);
                                  return;
                              }
                              self->read_response();
                          });
    }

    void read_response() {
        parser_.body_limit(kMaxGenerateBytes);
        http::async_read(socket_, buffer_, parser_,
                         [self = shared_from_this()](beast::error_code ec, std::size_t) {
                             if (ec || self->parser_.get().result() != http::status::ok) {
                                 self->finish(std::nullopt);
                                 return;
                             }
                             self->finish(parse_generate_response(self->parser_.get().body()));
                         });
    }

    void finish(std::optional<std::string> response) {
        if (finished_) {
            return;
        }
        finished_ = true;
        beast::error_code ignored;
        deadline_.cancel(ignored);
        resolver_.cancel();
        socket_.shutdown(tcp::socket::shutdown_both, ignored);
        socket_.close(ignored);
        done_(std::move(response));
    }

    Endpoint endpoint_;
    std::string prompt_;
    GenerateCallback done_;
    tcp::resolver resolver_;
    tcp::socket socket_;
    net::steady_timer deadline_;
    http::request<http::string_body> request_;
    beast::flat_buffer buffer_;
    http::response_parser<http::string_body> parser_;
    bool finished_ = false;
};

} // namespace

std::string build_expansion_prompt(std::string_view query, std::size_t max_terms) {
    return "List " + std::to_string(max_terms) + " English synonyms or closely related words for the "
           "search term \"" + std::string(query) + "\". Output only the words, separated by commas, "
           "no explanations.\nSynonyms:";
}

std::optional<std::string> parse_generate_response(std::string_view body) {
    boost::system::error_code ec;
    const auto value = boost::json::parse(body, ec);
    if (ec || !value.is_object()) {
        return std::nullopt;
    }
    const auto *response = value.as_object().if_contains("response");
    if (response == nullptr || !response->is_string()) {
        return std::nullopt;
    }
    return std::string(response->as_string());
}

namespace {

// Повертає одне слово-кандидат з фрагмента відповіді або порожній рядок. Фрагмент приймається
// лише якщо після відкидання маркера списку ("1.", "2)") і преамбули до останнього ':'
// лишається рівно одне слово з літер чи цифр, а не числа.
std::string single_word_candidate(std::string_view fragment) {
    std::size_t begin = 0;
    while (begin < fragment.size() && fragment[begin] == ' ') {
        ++begin;
    }
    std::size_t digits_end = begin;
    while (digits_end < fragment.size() && fragment[digits_end] >= '0' &&
           fragment[digits_end] <= '9') {
        ++digits_end;
    }
    if (digits_end > begin && digits_end < fragment.size() &&
        (fragment[digits_end] == '.' || fragment[digits_end] == ')')) {
        begin = digits_end + 1;
    }
    fragment = fragment.substr(begin);
    const std::size_t colon = fragment.rfind(':');
    if (colon != std::string_view::npos) {
        fragment = fragment.substr(colon + 1);
    }
    const std::vector<std::string> words = index::tokenize(fragment);
    if (words.size() != 1) {
        return {};
    }
    const std::string &word = words.front();
    const bool all_digits =
        std::all_of(word.begin(), word.end(), [](char c) { return c >= '0' && c <= '9'; });
    return all_digits ? std::string() : word;
}

} // namespace

std::vector<std::string> parse_expansion_terms(std::string_view model_text,
                                               const std::vector<std::string> &query_terms,
                                               std::size_t max_terms) {
    // Відповідь розбиваємо на фрагменти по ',', ';' і переносу рядка. Преамбула або нумерація
    // дають багатослівні фрагменти, які відкидаються, а не стають термами розширення.
    std::vector<std::string> terms;
    std::size_t start = 0;
    while (start <= model_text.size() && terms.size() < max_terms) {
        std::size_t stop = model_text.find_first_of(",;\n", start);
        if (stop == std::string_view::npos) {
            stop = model_text.size();
        }
        std::string token = single_word_candidate(model_text.substr(start, stop - start));
        start = stop + 1;
        if (token.empty()) {
            continue;
        }
        const bool in_query =
            std::find(query_terms.begin(), query_terms.end(), token) != query_terms.end();
        const bool seen = std::find(terms.begin(), terms.end(), token) != terms.end();
        if (!in_query && !seen) {
            terms.push_back(std::move(token));
        }
    }
    return terms;
}

void generate(net::io_context &ioc, Endpoint endpoint, std::string prompt,
              std::chrono::milliseconds timeout, GenerateCallback done) {
    std::make_shared<Generate>(ioc, std::move(endpoint), std::move(prompt), std::move(done))
        ->run(timeout);
}

ExpansionFunction make_expansion_handler(net::io_context &ioc, Endpoint endpoint,
                                         std::chrono::milliseconds timeout, std::size_t max_terms) {
    return [&ioc, endpoint = std::move(endpoint), timeout, max_terms](std::string query,
                                                                      ExpansionDone done) {
        const std::vector<std::string> query_terms = index::tokenize(query);
        generate(
            ioc, endpoint, build_expansion_prompt(query, max_terms), timeout,
            [query_terms, max_terms, done = std::move(done)](std::optional<std::string> response) {
                // FR-021: збій логується; пошук продовжується без розширення.
                if (!response) {
                    std::cerr << "ollama: expansion unavailable, keyword search only\n";
                    done({});
                    return;
                }
                std::vector<std::string> terms =
                    parse_expansion_terms(*response, query_terms, max_terms);
                if (terms.empty()) {
                    std::cerr << "ollama: no usable expansion terms, keyword search only\n";
                }
                done(std::move(terms));
            });
    };
}

} // namespace search::ollama
