#pragma once

#include "ollama/endpoint.hpp"

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace search::ollama {

// Промпт розширення запиту (ПЛЕЙСХОЛДЕР тексту, OQ-14: текст промпту ще не затверджено).
std::string build_expansion_prompt(std::string_view query, std::size_t max_terms);

// Витягує поле "response" з тіла відповіді /api/generate (stream=false). nullopt — некоректна
// відповідь.
std::optional<std::string> parse_generate_response(std::string_view body);

// Терми розширення: токенізація тексту моделі, без термів запиту, без дублів, не більше
// max_terms, у порядку появи (FR-019, OQ-14).
std::vector<std::string> parse_expansion_terms(std::string_view model_text,
                                               const std::vector<std::string> &query_terms,
                                               std::size_t max_terms);

using GenerateCallback = std::function<void(std::optional<std::string> response)>;

// Асинхронний POST /api/generate з тайм-аутом. done викликається рівно один раз: з текстом
// відповіді або nullopt (збій з'єднання, тайм-аут, HTTP не 200, некоректний JSON).
void generate(boost::asio::io_context &ioc, Endpoint endpoint, std::string prompt,
              std::chrono::milliseconds timeout, GenerateCallback done);

} // namespace search::ollama
