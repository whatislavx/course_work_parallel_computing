#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace search::cli {

// Аргументи CLI (FR-029, FR-030). Адресу сервера задає --url або SEARCH_SERVER_URL.
struct CliOptions {
    std::string server_url = "http://127.0.0.1:8080";
    std::string query;
    bool help = false;
};

// Розбирає аргументи без імені програми: [--url URL] [--help] ЗАПИТ. Повертає порожній рядок
// або повідомлення про помилку використання (код виходу 2).
std::string parse_arguments(const std::vector<std::string> &args, CliOptions *options);

struct ServerAddress {
    std::string host;
    std::string port;
};

// Розбирає http://host:port (шлях не допускається). nullopt — некоректна адреса.
std::optional<ServerAddress> parse_server_url(std::string_view url);

// Текст виводу для тіла 200 EP-03: по одному результату на блок або повідомлення про відсутність.
std::string format_results(std::string_view response_body, std::string_view query);

// Виконує пошук і пише результат у out, помилки — у err. Повертає код виходу (FR-030):
// 0 — успіх (у тому числі порожній результат), 1 — помилка сервера чи недоступність,
// 2 — помилка використання.
int run_search(const CliOptions &options, std::ostream &out, std::ostream &err);

} // namespace search::cli
