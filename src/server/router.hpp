#pragma once

#include <boost/beast/http.hpp>

#include <string_view>

namespace search::server {

using Request = boost::beast::http::request<boost::beast::http::string_body>;
using Response = boost::beast::http::response<boost::beast::http::string_body>;

// Відповідь помилки у форматі 06 §2.4: {"error":{"code","message"}}.
Response error_response(boost::beast::http::status status, std::string_view code,
                        std::string_view message);

// Маршрутизація за шляхом і методом (06 §2.3). Розмір тіла перевіряє сесія.
Response route(const Request &request);

} // namespace search::server
