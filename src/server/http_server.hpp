#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace search::server {

struct ServerOptions {
    std::uint16_t port = 8080; // 0 — операційна система обирає порт (для тестів)
    std::size_t max_request_body_bytes = 1 << 20;
    std::chrono::milliseconds request_timeout{10000};
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
