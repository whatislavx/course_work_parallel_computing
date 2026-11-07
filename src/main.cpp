// Точка входу: читає конфігурацію з оточення і виводить її (TCK-002). Сервер — у TCK-003.
#include "config/config.hpp"

#include <cstdlib>
#include <iostream>

int main() {
    try {
        const auto cfg = search::config::load_config_from_environment();
        std::cout << "search_server config: port=" << cfg.port
                  << " max_request_body_bytes=" << cfg.max_request_body_bytes << '\n';
        return EXIT_SUCCESS;
    } catch (const search::config::ConfigError &e) {
        std::cerr << "config error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
