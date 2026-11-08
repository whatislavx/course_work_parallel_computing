#include "cli/search_cli.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

#include <cstdio>
#include <exception>
#include <ostream>
#include <string>
#include <utility>

namespace search::cli {
namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// Результат HTTP-запиту: статус або причина відсутності відповіді.
struct HttpResult {
    bool reached = false; // з'єднання встановлено і відповідь прочитано
    unsigned status = 0;
    std::string body;
};

HttpResult post_search(const ServerAddress &address, const std::string &json_body) {
    HttpResult result;
    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        tcp::socket socket(ioc);
        net::connect(socket, resolver.resolve(address.host, address.port));
        http::request<http::string_body> req{http::verb::post, "/api/search", 11};
        req.set(http::field::host, address.host + ":" + address.port);
        req.set(http::field::content_type, "application/json");
        req.set(http::field::user_agent, "search-cli");
        req.body() = json_body;
        req.prepare_payload();
        http::write(socket, req);
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(socket, buffer, res);
        result.reached = true;
        result.status = res.result_int();
        result.body = std::move(res.body());
    } catch (const std::exception &) {
        // Недоступність сервера: reached залишається false.
    }
    return result;
}

std::string build_search_body(std::string_view query) {
    boost::json::object body;
    body["query"] = std::string(query);
    body["mode"] = "keyword";
    return boost::json::serialize(body);
}

// Повідомлення з тіла помилки {"error":{"code","message"}} або загальний опис статусу.
std::string error_message(const HttpResult &result) {
    boost::system::error_code ec;
    const boost::json::value value = boost::json::parse(result.body, ec);
    if (!ec && value.is_object()) {
        if (const auto *error = value.as_object().if_contains("error");
            error != nullptr && error->is_object()) {
            if (const auto *message = error->as_object().if_contains("message");
                message != nullptr && message->is_string()) {
                return std::string(message->as_string());
            }
        }
    }
    return "server returned HTTP " + std::to_string(result.status);
}

} // namespace

std::string parse_arguments(const std::vector<std::string> &args, CliOptions *options) {
    std::optional<std::string> query;
    bool options_ended = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string &arg = args[i];
        if (!options_ended && arg == "--") {
            options_ended = true;
        } else if (!options_ended && (arg == "--help" || arg == "-h")) {
            options->help = true;
        } else if (!options_ended && arg == "--url") {
            if (i + 1 >= args.size()) {
                return "--url requires a value";
            }
            options->server_url = args[++i];
        } else if (!options_ended && !arg.empty() && arg[0] == '-') {
            return "unknown option: " + arg;
        } else {
            if (query) {
                return "exactly one query argument is expected";
            }
            query = arg;
        }
    }
    if (options->help) {
        return {};
    }
    if (!query) {
        return "missing query argument";
    }
    if (query->find_first_not_of(" \t\r\n") == std::string::npos) {
        return "query must not be empty";
    }
    options->query = *query;
    return {};
}

std::optional<ServerAddress> parse_server_url(std::string_view url) {
    constexpr std::string_view kScheme = "http://";
    if (url.substr(0, kScheme.size()) != kScheme) {
        return std::nullopt;
    }
    url.remove_prefix(kScheme.size());
    const std::size_t colon = url.rfind(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 == url.size()) {
        return std::nullopt;
    }
    ServerAddress address{std::string(url.substr(0, colon)), std::string(url.substr(colon + 1))};
    if (address.host.find('/') != std::string::npos) {
        return std::nullopt;
    }
    for (char c : address.port) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
    }
    return address;
}

// Повертає std::nullopt, якщо відповідь не відповідає схемі {"results":[{doc_id,score,snippet}]}.
std::optional<std::string> format_results(std::string_view response_body, std::string_view query) {
    boost::system::error_code ec;
    const boost::json::value value = boost::json::parse(response_body, ec);
    std::string text;
    const boost::json::array *results = nullptr;
    if (!ec && value.is_object()) {
        if (const auto *array = value.as_object().if_contains("results");
            array != nullptr && array->is_array()) {
            results = &array->as_array();
        }
    }
    if (results == nullptr) {
        return std::nullopt;
    }
    if (results->empty()) {
        text = "No results found for \"" + std::string(query) + "\".\n";
        return text;
    }
    std::size_t rank = 1;
    for (const boost::json::value &item : *results) {
        if (!item.is_object()) {
            return std::nullopt;
        }
        const boost::json::object &hit = item.as_object();
        const auto *score_value = hit.if_contains("score");
        const auto *doc_id_value = hit.if_contains("doc_id");
        const auto *snippet_value = hit.if_contains("snippet");
        if (score_value == nullptr || !score_value->is_number() || doc_id_value == nullptr ||
            !doc_id_value->is_string() || snippet_value == nullptr || !snippet_value->is_string()) {
            return std::nullopt;
        }
        char score[32];
        std::snprintf(score, sizeof score, "%.4f", score_value->to_number<double>());
        text += std::to_string(rank++) + ". " + std::string(doc_id_value->as_string()) +
                " (score " + score + ")\n   " + std::string(snippet_value->as_string()) + "\n";
    }
    return text;
}

int run_search(const CliOptions &options, std::ostream &out, std::ostream &err) {
    const std::optional<ServerAddress> address = parse_server_url(options.server_url);
    if (!address) {
        err << "error: invalid server URL (expected http://host:port): " << options.server_url
            << '\n';
        return 2;
    }
    const HttpResult result = post_search(*address, build_search_body(options.query));
    if (!result.reached) {
        err << "error: search server is unavailable at " << options.server_url << '\n';
        return 1;
    }
    if (result.status != 200) {
        err << "error: " << error_message(result) << '\n';
        return 1;
    }
    const std::optional<std::string> text = format_results(result.body, options.query);
    if (!text) {
        err << "error: malformed response from search server\n";
        return 1;
    }
    out << *text;
    return 0;
}

} // namespace search::cli
