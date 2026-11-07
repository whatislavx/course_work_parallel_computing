#include "config/config.hpp"
#include "ollama/probe.hpp"
#include "server/http_server.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <chrono>
#include <csignal>
#include <exception>
#include <functional>
#include <iostream>

int main() {
    try {
        const auto cfg = search::config::load_config_from_environment();
        const auto ollama_endpoint =
            search::ollama::parse_ollama_url(cfg.ollama_url, cfg.ollama_model);
        if (!ollama_endpoint) {
            throw search::config::ConfigError("OLLAMA_URL: некоректне значення");
        }
        const auto ollama_timeout = std::chrono::milliseconds(cfg.health_check_timeout_ms);

        boost::asio::io_context ioc{1};

        search::server::ServerOptions options;
        options.port = cfg.port;
        options.max_request_body_bytes = cfg.max_request_body_bytes;
        options.request_timeout = std::chrono::milliseconds(cfg.request_timeout_ms);
        options.ollama_check = [&ioc, endpoint = *ollama_endpoint,
                                ollama_timeout](std::function<void(bool)> done) {
            search::ollama::probe(ioc, endpoint, ollama_timeout, std::move(done));
        };

        search::server::HttpServer server(ioc, options);
        server.start();

        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&server, &ioc](const boost::system::error_code &, int) {
            server.stop();
            ioc.stop();
        });

        std::cout << "search_server listening on port " << server.port() << '\n';
        ioc.run();
        return 0;
    } catch (const search::config::ConfigError &e) {
        std::cerr << "config error: " << e.what() << '\n';
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;
    }
}
