#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace search::config {

// Некоректна конфігурація: повідомлення називає змінну оточення і причину.
class ConfigError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Джерело значень змінних оточення; порожній optional означає «не задано».
using EnvLookup = std::function<std::optional<std::string>(std::string_view)>;

// Значення за замовчуванням позначені тимчасовими там, де параметр у специфікації
// має статус «не визначено» (NFR-019, 06 §2.7). Їх треба затвердити (OQ-14, OQ-20, OQ-27).
struct Config {
    std::uint16_t port = 8080; // SERVER_PORT
    std::string dataset_dir;   // DATASET_DIR; порожньо = не задано (OQ-01)
    std::size_t default_index_threads = 0; // DEFAULT_INDEX_THREADS; 0 = логічні процесори (OQ-07)
    std::size_t max_request_body_bytes = 1 << 20; // MAX_REQUEST_BODY_BYTES; тимчасово 1 МіБ (OQ-27)
    std::size_t health_check_timeout_ms = 2000; // HEALTH_CHECK_TIMEOUT_MS; тимчасово (OQ-14)
    std::size_t request_timeout_ms =
        10000; // REQUEST_TIMEOUT_MS; тайм-аут читання/запису, тимчасово
    std::string ollama_url = "http://ollama:11434"; // OLLAMA_URL
    std::string ollama_model; // OLLAMA_MODEL; порожньо = лише доступність (OQ-13)
};

// Розбирає і валідує конфігурацію з довільного джерела (для тестів і для оточення).
Config parse_config(const EnvLookup &lookup);

// Читає змінні з реального оточення процесу.
Config load_config_from_environment();

} // namespace search::config
