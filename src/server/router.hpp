#pragma once

#include <boost/beast/http.hpp>

#include <string_view>

namespace search::server {

using Request = boost::beast::http::request<boost::beast::http::string_body>;
using Response = boost::beast::http::response<boost::beast::http::string_body>;

// Відповідь помилки у форматі 06 §2.4: {"error":{"code","message"}}.
Response error_response(boost::beast::http::status status, std::string_view code,
                        std::string_view message);

// Чи є запит GET /api/health (його статус Ollama перевіряється асинхронно).
bool is_health_request(const Request &request);

// Маршрутизація за шляхом і методом (06 §2.3). ollama_available — результат перевірки Ollama
// для EP-01; для інших ендпоінтів не використовується. Розмір тіла перевіряє сесія.
Response route(const Request &request, bool ollama_available);

} // namespace search::server
