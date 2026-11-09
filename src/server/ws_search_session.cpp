#include "server/ws_search_session.hpp"

#include "query/search_service.hpp"

#include <boost/asio/post.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string_view>
#include <utility>

namespace search::server {
namespace {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace json = boost::json;

// Виконує пошук і формує результат етапу. Винятки не виходять назовні (06 §4.5).
KeywordOutcome run_keyword_search(const SearchHandler &handler, const std::string &query,
                                  const std::vector<std::string> &terms) {
    KeywordOutcome outcome;
    try {
        if (!handler) {
            outcome.error_code = "internal_error";
            outcome.message = "search is not configured";
            return outcome;
        }
        const auto started = std::chrono::steady_clock::now();
        outcome.hits = handler(query, terms);
        outcome.time_ms =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - started)
                                           .count());
        outcome.ok = true;
    } catch (const query::IndexNotReadyError &) {
        outcome.error_code = "index_not_ready";
        outcome.message = "index is not built yet";
    } catch (const std::exception &e) {
        std::cerr << "ws search failed: " << e.what() << '\n';
        outcome.error_code = "internal_error";
        outcome.message = "internal server error";
    } catch (...) {
        outcome.error_code = "internal_error";
        outcome.message = "internal server error";
    }
    return outcome;
}

std::string serialize_error(std::string_view code, std::string_view message) {
    json::object data;
    data["code"] = std::string(code);
    data["message"] = std::string(message);
    json::object event;
    event["stage"] = "error";
    event["data"] = std::move(data);
    return json::serialize(event);
}

std::string serialize_expansion(const std::string &query, const std::vector<std::string> &terms) {
    json::array expanded;
    for (const std::string &term : terms) {
        expanded.emplace_back(term);
    }
    json::object data;
    data["original"] = query;
    data["expanded_terms"] = std::move(expanded);
    json::object event;
    event["stage"] = "query_expansion";
    event["data"] = std::move(data);
    return json::serialize(event);
}

std::string serialize_keyword(const std::vector<query::SearchHit> &hits, std::uint64_t time_ms) {
    json::array results;
    for (const query::SearchHit &hit : hits) {
        json::object item;
        item["doc_id"] = hit.doc_id;
        item["score"] = hit.score;
        item["title"] = hit.title;
        item["snippet"] = hit.snippet;
        results.emplace_back(std::move(item));
    }
    json::object data;
    data["results"] = std::move(results);
    data["time_ms"] = time_ms;
    json::object event;
    event["stage"] = "keyword_search";
    event["data"] = std::move(data);
    return json::serialize(event);
}

// Подія done — рівно {"stage":"done"} без data (AD-09, OQ-31).
std::string serialize_done() {
    json::object event;
    event["stage"] = "done";
    return json::serialize(event);
}

bool is_trim_char(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Розбирає повідомлення клієнта WS-C-01. Повертає код помилки або порожній рядок, якщо
// запит валідний; query отримує обрізаний текст. Порядок перевірок — як у REST (06 §4.5).
std::string parse_client_message(const std::string &text, std::size_t max_query_length,
                                 std::string *query) {
    boost::system::error_code ec;
    const json::value value = json::parse(text, ec);
    if (ec || !value.is_object()) {
        return "invalid_json";
    }
    const json::object &object = value.as_object();
    const json::value *field = object.if_contains("query");
    if (field == nullptr || !field->is_string()) {
        return "invalid_query";
    }
    std::string_view trimmed = field->as_string();
    while (!trimmed.empty() && is_trim_char(trimmed.front())) {
        trimmed.remove_prefix(1);
    }
    while (!trimmed.empty() && is_trim_char(trimmed.back())) {
        trimmed.remove_suffix(1);
    }
    if (trimmed.empty() || trimmed.size() > max_query_length) {
        return "invalid_query";
    }
    query->assign(trimmed);
    return {};
}

} // namespace

WsSearchSession::WsSearchSession(beast::tcp_stream stream, WsSearchDeps deps)
    : ws_(std::move(stream)), deps_(std::move(deps)) {}

WsSearchSession::~WsSearchSession() {
    if (deps_.active_sessions) {
        deps_.active_sessions->fetch_sub(1);
    }
}

void WsSearchSession::start(Request request) {
    // Таймер простою не встановлюється (OQ-12): сесія живе, доки її не закриє клієнт.
    beast::get_lowest_layer(ws_).expires_never();
    // Beast за замовчуванням приймає кадри до 16 MiB. Ліміт — із запасом над максимальним
    // запитом у JSON-екрануванні (\uXXXX); перевищення закриває з'єднання кодом 1009.
    ws_.read_message_max(std::max<std::size_t>(64 * 1024, 8 * deps_.max_query_length + 1024));
    ws_.async_accept(request, [self = shared_from_this()](beast::error_code ec) {
        if (!ec) {
            self->read_next();
        }
    });
}

void WsSearchSession::read_next() {
    ws_.async_read(buffer_, [self = shared_from_this()](beast::error_code ec, std::size_t) {
        self->on_read(ec);
    });
}

void WsSearchSession::on_read(beast::error_code ec) {
    if (ec) {
        // Закриття клієнтом або помилка протоколу: сесія завершується без відповіді.
        return;
    }
    const std::string text = beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    // Читання не чекає пайплайну: повідомлення під час виконання отримує pipeline_busy, а не
    // лежить у буфері (BDD-WS-08). got_text() треба прочитати до наступного async_read.
    const bool is_text = ws_.got_text();
    read_next();
    if (!is_text) {
        enqueue(serialize_error("invalid_json", "binary frames are not supported"));
        return;
    }
    handle_message(text);
}

void WsSearchSession::handle_message(const std::string &text) {
    std::string query;
    const std::string code = parse_client_message(text, deps_.max_query_length, &query);
    // Порядок перевірок як у 06 §4.5: формат і поля, потім зайнятість, потім індекс.
    if (!code.empty()) {
        enqueue(serialize_error(code, code == "invalid_json" ? "message is not a JSON object"
                                                             : "query must be a non-empty string"));
        return;
    }
    if (busy_) {
        enqueue(serialize_error("pipeline_busy", "previous query is still running"));
        return;
    }
    // Перевірка індексу до виклику Ollama: за відсутності індексу query_expansion не надсилається.
    if (deps_.index_ready && !deps_.index_ready()) {
        enqueue(serialize_error("index_not_ready", "index is not built yet"));
        return;
    }
    busy_ = true;
    try {
        if (deps_.expansion) {
            deps_.expansion(query,
                            [self = shared_from_this(), query](std::vector<std::string> terms) {
                                self->on_expansion(query, std::move(terms));
                            });
            return;
        }
        on_expansion(query, {});
    } catch (const std::exception &e) {
        std::cerr << "ws pipeline failed: " << e.what() << '\n';
        fail_pipeline("internal_error", "internal server error");
    }
}

void WsSearchSession::fail_pipeline(std::string_view code, std::string_view message) {
    busy_ = false;
    enqueue(serialize_error(code, message));
}

void WsSearchSession::on_expansion(const std::string &query, std::vector<std::string> terms) {
    enqueue(serialize_expansion(query, terms));
    run_keyword(query, terms);
}

void WsSearchSession::run_keyword(const std::string &query, const std::vector<std::string> &terms) {
    // Пошук виконується у пулі (TCK-019); результат повертається в io_context через post.
    auto executor = ws_.get_executor();
    // self переміщується в post: копія в задачі пулу звільняється до того, як сесія
    // знищується, тож ~WsSearchSession виконується лише в потоці io_context.
    auto work = [self = shared_from_this(), executor, handler = deps_.search, query,
                 terms]() mutable {
        KeywordOutcome outcome = run_keyword_search(handler, query, terms);
        net::post(executor, [self = std::move(self), outcome = std::move(outcome)]() mutable {
            self->on_keyword(std::move(outcome));
        });
    };
    try {
        if (deps_.executor) {
            deps_.executor(std::move(work));
        } else {
            work();
        }
    } catch (const std::exception &e) {
        std::cerr << "ws search dispatch failed: " << e.what() << '\n';
        fail_pipeline("internal_error", "internal server error");
    }
}

void WsSearchSession::on_keyword(KeywordOutcome outcome) {
    busy_ = false;
    if (!outcome.ok) {
        enqueue(serialize_error(outcome.error_code, outcome.message));
    } else {
        enqueue(serialize_keyword(outcome.hits, outcome.time_ms));
        enqueue(serialize_done());
    }
}

void WsSearchSession::enqueue(std::string message) {
    // Після початку закриття нові кадри не відправляємо: закриття має пріоритет (TCK-039).
    if (close_requested_) {
        return;
    }
    outgoing_.push_back(std::move(message));
    if (!writing_) {
        write_next();
    }
}

void WsSearchSession::write_next() {
    writing_ = true;
    ws_.async_write(
        net::buffer(outgoing_.front()),
        [self = shared_from_this()](beast::error_code ec, std::size_t) { self->on_written(ec); });
}

void WsSearchSession::on_written(beast::error_code ec) {
    writing_ = false;
    if (ec) {
        // Клієнт зник: черга більше не потрібна.
        outgoing_.clear();
        return;
    }
    outgoing_.pop_front();
    if (close_requested_) {
        // Запис завершився до запиту на закриття: решту черги відкидаємо, закриваємо з 1001.
        begin_close();
        return;
    }
    if (!outgoing_.empty()) {
        write_next();
    }
}

void WsSearchSession::close_going_away() {
    if (close_requested_) {
        return;
    }
    close_requested_ = true;
    // Під час запису закриття починає on_written, щоб не було двох одночасних записів.
    if (!writing_) {
        begin_close();
    }
}

void WsSearchSession::begin_close() {
    outgoing_.clear();
    writing_ = true;
    ws_.async_close(beast::websocket::close_code::going_away,
                    [self = shared_from_this()](beast::error_code) { self->writing_ = false; });
}

void WsSessionRegistry::add(const std::shared_ptr<WsSearchSession> &session) {
    prune();
    sessions_.push_back(session);
}

void WsSessionRegistry::close_all_going_away() {
    prune();
    for (const std::weak_ptr<WsSearchSession> &weak : sessions_) {
        if (auto session = weak.lock()) {
            session->close_going_away();
        }
    }
}

std::size_t WsSessionRegistry::open_count() {
    prune();
    return sessions_.size();
}

void WsSessionRegistry::prune() {
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
                       [](const std::weak_ptr<WsSearchSession> &weak) { return weak.expired(); }),
        sessions_.end());
}

} // namespace search::server
