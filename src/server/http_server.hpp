#pragma once

#include "index/index_manager.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace search::server {

// Асинхронна перевірка Ollama: викликає callback рівно один раз з результатом.
using OllamaCheck = std::function<void(std::function<void(bool)>)>;

// Запуск побудови для EP-02. Повертає false, якщо побудова вже триває (409). Викликає done
// рівно один раз після завершення побудови (з потоку побудови). Може кинути виняток, якщо
// побудову не вдалося запустити (500 internal_error).
using IndexStarter = std::function<bool(std::size_t threads, index::BuildCallback done)>;

struct ServerOptions {
    std::uint16_t port = 8080; // 0 — операційна система обирає порт (для тестів)
    std::size_t max_request_body_bytes = 1 << 20;
    std::chrono::milliseconds request_timeout{10000};
    OllamaCheck ollama_check; // порожньо = Ollama вважається недоступною

    // EP-02: порожньо = ендпоінт побудови відповідає 500 internal_error.
    IndexStarter index_start;
    std::size_t default_index_threads = 1; // DEFAULT_INDEX_THREADS (після розв'язання 0)
    std::size_t max_index_threads = 1;     // MAX_INDEX_THREADS
};

// Один запит на з'єднання (Connection: close). Keep-alive і паралелізм — у TCK-019.
// Потокова умова: усі виклики (включно зі stop) мають відбуватися в потоці, що крутить io_context.
class HttpServer {
  public:
    HttpServer(boost::asio::io_context &ioc, const ServerOptions &options);

    void start();
    void stop();
    std::uint16_t port() const;

  private:
    void do_accept();

    ServerOptions options_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::steady_timer retry_timer_;
};

} // namespace search::server
