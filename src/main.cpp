#include "config/config.hpp"
#include "index/index_manager.hpp"
#include "ollama/probe.hpp"
#include "server/http_server.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <exception>
#include <functional>
#include <iostream>
#include <thread>

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
        // Менеджер оголошено після ioc: він знищується першим і приєднує потік побудови,
        // доки io_context ще живий для відкладених відповідей.
        search::index::IndexManager index_manager;

        search::server::ServerOptions options;
        options.port = cfg.port;
        options.max_request_body_bytes = cfg.max_request_body_bytes;
        options.request_timeout = std::chrono::milliseconds(cfg.request_timeout_ms);
        options.ollama_check = [&ioc, endpoint = *ollama_endpoint,
                                ollama_timeout](std::function<void(bool)> done) {
            search::ollama::probe(ioc, endpoint, ollama_timeout, std::move(done));
        };

        // DEFAULT_INDEX_THREADS = 0 означає «логічні процесори» (OQ-07).
        const std::size_t hardware = std::max<std::size_t>(1, std::thread::hardware_concurrency());
        options.default_index_threads = cfg.default_index_threads == 0
                                            ? std::min(hardware, cfg.max_index_threads)
                                            : cfg.default_index_threads;
        options.max_index_threads = cfg.max_index_threads;
        options.index_start = [&index_manager, dataset_dir = cfg.dataset_dir](
                                  std::size_t threads, search::index::BuildCallback done) {
            return index_manager.start_build(dataset_dir, threads, std::move(done));
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
