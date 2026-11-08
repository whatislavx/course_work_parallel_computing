#include "config/config.hpp"
#include "index/index_manager.hpp"
#include "index/tokenizer.hpp"
#include "ollama/generate.hpp"
#include "ollama/probe.hpp"
#include "pool/thread_pool.hpp"
#include "query/search_service.hpp"
#include "server/http_server.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
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
        // Пул пошуку (TCK-019, варіант A): важка робота не блокує io_context. Місткість черги
        // не менша за ліміт з'єднань, тож подача задач не блокується.
        const std::size_t logical_cpus =
            std::max<std::size_t>(1, std::thread::hardware_concurrency());
        const std::size_t search_threads =
            cfg.search_worker_threads == 0 ? logical_cpus : cfg.search_worker_threads;
        // search_service оголошено до пулу: пул знищується раніше і дочищає чергу, поки сервіс
        // ще живий (інакше задачі звертаються до знищеного об'єкта).
        std::unique_ptr<search::query::SearchService> search_service;
        search::pool::ThreadPool search_pool(search_threads, cfg.max_concurrent_connections);

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

        options.stats_provider = [&index_manager] { return index_manager.stats(); };

        // EP-03: пошук за OR-семантикою і TF-IDF; тексти для snippet — з DATASET_DIR (BL-02).
        search_service = std::make_unique<search::query::SearchService>(
            [&index_manager] { return index_manager.snapshot(); }, cfg.dataset_dir,
            search::query::SearchOptions{cfg.search_top_k, cfg.snippet_max_chars});
        options.search_handler = [&search_service](std::string_view text,
                                                   const std::vector<std::string> &extra) {
            return search_service->search(text, extra);
        };
        options.max_query_length = cfg.max_query_length;
        options.max_concurrent_connections = cfg.max_concurrent_connections;
        options.search_executor = [&search_pool](std::function<void()> task) {
            search_pool.submit(std::move(task));
        };

        // EP-03, mode "expanded" (OQ-02): розширення через Ollama /api/generate. Без моделі
        // розширення вимкнено; збій або тайм-аут дає пошук без розширення (FR-021).
        if (!ollama_endpoint->model.empty()) {
            const auto expansion_timeout = std::chrono::milliseconds(cfg.ollama_timeout_ms);
            const std::size_t max_terms = cfg.max_expanded_terms;
            options.expansion_handler =
                [&ioc, endpoint = *ollama_endpoint, expansion_timeout,
                 max_terms](std::string text, std::function<void(std::vector<std::string>)> done) {
                    const std::vector<std::string> query_terms = search::index::tokenize(text);
                    search::ollama::generate(
                        ioc, endpoint, search::ollama::build_expansion_prompt(text, max_terms),
                        expansion_timeout,
                        [query_terms, max_terms,
                         done = std::move(done)](std::optional<std::string> response) {
                            if (!response) {
                                done({});
                                return;
                            }
                            done(search::ollama::parse_expansion_terms(*response, query_terms,
                                                                       max_terms));
                        });
                };
        }

        search::server::HttpServer server(ioc, options);
        server.start();

        // Відома обмеження (узгоджено): під час побудови індексу SIGTERM зупиняє io_context,
        // але деструктор IndexManager чекає завершення побудови без скасування. Процес
        // завершується після неї; у Docker Compose stop_grace_period має бути не меншим за
        // очікувану тривалість побудови (див. README, OQ-20).
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
