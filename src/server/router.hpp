#pragma once

#include "index/index_manager.hpp"
#include "query/search_service.hpp"

#include <boost/beast/http.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace search::server {

using Request = boost::beast::http::request<boost::beast::http::string_body>;
using Response = boost::beast::http::response<boost::beast::http::string_body>;

// Відповідь помилки у форматі 06 §2.4: {"error":{"code","message"}}.
Response error_response(boost::beast::http::status status, std::string_view code,
                        std::string_view message);

// Чи є запит GET /api/health (його статус Ollama перевіряється асинхронно).
bool is_health_request(const Request &request);

// Чи є запит POST /api/index (його тіло читає сесія, а відповідь відкладається до кінця побудови).
bool is_index_request(const Request &request);

// Тіло EP-03 після розбору.
struct SearchRequest {
    std::string query; // обрізано пробіли, довжина ≥ 1 і ≤ max_query_length байт
    bool expanded = false; // mode == "expanded": розширення через Ollama (OQ-02)
};

// Розбирає тіло EP-03 або повертає готову відповідь 400 (invalid_json, invalid_query,
// invalid_mode, unsupported_mode). Режим "expanded" поки не реалізовано (LLM, окремий тікет).
std::variant<SearchRequest, Response> parse_search_body(std::string_view body,
                                                        std::size_t max_query_length);

// Чи є запит POST /api/search (обробляє сесія: тіло, пошук, відповідь).
bool is_search_request(const Request &request);

// Чи є запит GET /ws/search з Upgrade: websocket (сесія передає сокет WebSocket-сесії).
bool is_ws_search_request(const Request &request);

// Відповідь EP-03 з результатами пошуку та часом виконання (06 EP-03).
Response search_response(const std::vector<query::SearchHit> &hits, std::uint64_t time_ms);

// Розбирає тіло EP-02. Повертає кількість потоків або готову відповідь 400.
// Порожнє тіло чи відсутнє поле threads → default_threads.
std::variant<std::size_t, Response>
parse_index_body(std::string_view body, std::size_t default_threads, std::size_t max_threads);

// Чи є запит GET /api/stats (тіло відповіді формує сесія зі статистики індексу).
bool is_stats_request(const Request &request);

// Відповідь EP-04 з поточною статистикою індексу (06 EP-04).
Response stats_response(const index::IndexStats &stats);

// Маршрутизація за шляхом і методом (06 §2.3). ollama_available — результат перевірки Ollama
// для EP-01; для інших ендпоінтів не використовується. Розмір тіла перевіряє сесія.
Response route(const Request &request, bool ollama_available);

} // namespace search::server
