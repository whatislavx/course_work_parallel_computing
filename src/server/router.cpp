#include "server/router.hpp"

#include <boost/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace search::server {
namespace {

namespace bh = boost::beast::http;

constexpr unsigned kHttpVersion = 11;

struct Route {
    std::string_view path;
    bh::verb method;
    bool implemented;
};

// Ендпоінти 06 §1. implemented=false → 501, доки не реалізовано відповідний тікет.
constexpr Route kRoutes[] = {
    {"/api/health", bh::verb::get, true},
    {"/api/index", bh::verb::post, true},
    {"/api/search", bh::verb::post, false},
    {"/api/stats", bh::verb::get, false},
};

std::string_view path_of(std::string_view target) { return target.substr(0, target.find('?')); }

Response json_response(bh::status status, std::string body) {
    Response res{status, kHttpVersion};
    res.set(bh::field::server, "search-engine");
    res.set(bh::field::content_type, "application/json; charset=utf-8");
    res.body() = std::move(body);
    return res;
}

Response health_response(bool ollama_available) {
    boost::json::object body{{"status", "ok"}, {"ollama", ollama_available}};
    return json_response(bh::status::ok, boost::json::serialize(body));
}

} // namespace

Response error_response(bh::status status, std::string_view code, std::string_view message) {
    boost::json::object error{{"code", code}, {"message", message}};
    boost::json::object body{{"error", error}};
    return json_response(status, boost::json::serialize(body));
}

bool is_health_request(const Request &request) {
    return path_of(request.target()) == "/api/health" && request.method() == bh::verb::get;
}

bool is_index_request(const Request &request) {
    return path_of(request.target()) == "/api/index" && request.method() == bh::verb::post;
}

std::variant<std::size_t, Response>
parse_index_body(std::string_view body, std::size_t default_threads, std::size_t max_threads) {
    if (body.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        return default_threads;
    }
    boost::system::error_code ec;
    boost::json::value parsed = boost::json::parse(body, ec);
    if (ec || !parsed.is_object()) {
        return error_response(bh::status::bad_request, "invalid_json",
                              "request body must be a JSON object");
    }
    const boost::json::object &object = parsed.as_object();
    const auto field = object.find("threads");
    if (field == object.end()) {
        return default_threads;
    }
    const boost::json::value &value = field->value();
    // Тип integer; дробові числа, рядки й вихід за межі — invalid_threads.
    if (value.is_int64()) {
        const std::int64_t requested = value.as_int64();
        if (requested < 1 || static_cast<std::uint64_t>(requested) > max_threads) {
            return error_response(bh::status::bad_request, "invalid_threads",
                                  "threads must be an integer from 1 to MAX_INDEX_THREADS");
        }
        return static_cast<std::size_t>(requested);
    }
    if (value.is_uint64()) {
        const std::uint64_t requested = value.as_uint64();
        if (requested > max_threads) {
            return error_response(bh::status::bad_request, "invalid_threads",
                                  "threads must be an integer from 1 to MAX_INDEX_THREADS");
        }
        return static_cast<std::size_t>(requested);
    }
    return error_response(bh::status::bad_request, "invalid_threads",
                          "threads must be an integer from 1 to MAX_INDEX_THREADS");
}

Response route(const Request &request, bool ollama_available) {
    const std::string_view path = path_of(request.target());

    bool path_known = false;
    std::string_view allowed_method;
    for (const Route &r : kRoutes) {
        if (r.path != path) {
            continue;
        }
        path_known = true;
        allowed_method = bh::to_string(r.method);
        if (r.method != request.method()) {
            continue;
        }
        if (!r.implemented) {
            return error_response(bh::status::not_implemented, "not_implemented",
                                  "endpoint is not implemented yet");
        }
        if (path == "/api/index") {
            // Сесія обробляє EP-02 до маршрутизації; сюди запит не доходить.
            return error_response(bh::status::internal_server_error, "internal_error",
                                  "index request routed incorrectly");
        }
        return health_response(ollama_available);
    }

    if (path_known) {
        Response res = error_response(bh::status::method_not_allowed, "method_not_allowed",
                                      "method is not allowed for this path");
        res.set(bh::field::allow, allowed_method);
        return res;
    }
    return error_response(bh::status::not_found, "not_found", "unknown path");
}

} // namespace search::server
