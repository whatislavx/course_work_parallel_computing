#pragma once

#include "ollama/endpoint.hpp"

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <functional>
#include <string_view>

namespace search::ollama {

using ProbeCallback = std::function<void(bool available)>;

// Асинхронна перевірка доступності: GET /api/tags. Успіх = HTTP 200 і, якщо задано модель,
// вона присутня у списку. done викликається рівно один раз, не пізніше timeout (AC-002.4).
void probe(boost::asio::io_context &ioc, Endpoint endpoint, std::chrono::milliseconds timeout,
           ProbeCallback done);

// Чи містить тіло відповіді /api/tags модель з іменем model (або model:latest).
bool models_contain(std::string_view tags_json, std::string_view model);

} // namespace search::ollama
