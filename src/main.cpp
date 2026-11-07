// Точка входу: конфігурація з оточення, HTTP-сервер з health (TCK-003).
#include "config/config.hpp"
#include "server/http_server.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>

int main() {
    try {
        const auto cfg = search::config::load_config_from_environment();

        boost::asio::io_context ioc{1};

        search::server::ServerOptions options;
        options.port = cfg.port;
        options.max_request_body_bytes = cfg.max_request_body_bytes;
        options.request_timeout = std::chrono::milliseconds(cfg.request_timeout_ms);

        search::server::HttpServer server(ioc, options);
        server.start();

        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&server, &ioc](const boost::system::error_code &, int) {
            server.stop();
            ioc.stop();
        });

        std::cout << "search_server listening on port " << server.port() << '\n';
        ioc.run();
        return EXIT_SUCCESS;
    } catch (const search::config::ConfigError &e) {
        std::cerr << "config error: " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception &e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
