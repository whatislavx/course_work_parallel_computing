#pragma once

#include "query/search_service.hpp"
#include "server/http_server.hpp"
#include "server/router.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace search::server {

// Залежності пайплайну WS-пошуку (TCK-022). Спільні з REST: ті самі обробники розширення і
// пошуку, той самий пул (TCK-019).
struct WsSearchDeps {
    ExpansionHandler expansion; // порожньо = розширення вимкнено
    SearchHandler search;       // порожньо = internal_error
    SearchExecutor executor;    // порожньо = пошук у потоці io_context
    std::function<bool()> index_ready; // порожньо = індекс вважається готовим
    std::size_t max_query_length = 256;
    // Лічильник з'єднань сервера (TCK-019). Збільшений вже в Session; сесія зменшує його у
    // своєму деструкторі.
    std::shared_ptr<std::atomic<std::size_t>> active_sessions;
};

// Результат етапу keyword_search: або hits і час, або код помилки (06 §4.5).
struct KeywordOutcome {
    bool ok = false;
    std::vector<query::SearchHit> hits;
    std::uint64_t time_ms = 0;
    std::string error_code;
    std::string message;
};

// WebSocket-сесія /ws/search (06 §4, WS-01…WS-E-04). Пайплайни однієї сесії виконуються
// послідовно: наступне повідомлення клієнта читається після done або error. Стан сесії живе в
// потоці io_context; важка робота (пошук) виконується через deps.executor і повертається в
// io_context через post.
class WsSearchSession : public std::enable_shared_from_this<WsSearchSession> {
  public:
    WsSearchSession(boost::beast::tcp_stream stream, WsSearchDeps deps);
    ~WsSearchSession();

    WsSearchSession(const WsSearchSession &) = delete;
    WsSearchSession &operator=(const WsSearchSession &) = delete;

    // Завершує WebSocket-рукостискання за HTTP-запитом Upgrade і починає читання повідомлень.
    void start(Request request);

  private:
    void read_next();
    void on_read(boost::beast::error_code ec);
    void handle_message(const std::string &text);
    void on_expansion(const std::string &query, std::vector<std::string> terms);
    void run_keyword(const std::string &query, const std::vector<std::string> &terms);
    // Завершує поточний пайплайн помилкою, не залишаючи сесію зайнятою (busy_).
    void fail_pipeline(std::string_view code, std::string_view message);
    void on_keyword(KeywordOutcome outcome);

    // Черга вихідних кадрів: WebSocket допускає лише одну операцію запису одночасно.
    void enqueue(std::string message);
    void write_next();
    void on_written(boost::beast::error_code ec);

    boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
    boost::beast::flat_buffer buffer_;
    WsSearchDeps deps_;
    std::deque<std::string> outgoing_; // Guarded by io_context (одночасно лише в потоці io)
    bool writing_ = false;
    bool busy_ = false; // пайплайн сесії ще виконується (FR-027, pipeline_busy)
};

} // namespace search::server
