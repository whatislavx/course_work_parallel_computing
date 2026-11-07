#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace search::ollama {

struct Endpoint {
    std::string host;
    std::string port; // десяткове число у вигляді рядка; за замовчуванням "80"
    std::string model; // порожньо = перевіряється лише доступність сервісу (OQ-13)
};

// Розбирає http://хост[:порт] або http://хост[:порт]/. Інші схеми і шляхи відхиляються.
std::optional<Endpoint> parse_ollama_url(std::string_view url, std::string_view model);

} // namespace search::ollama
