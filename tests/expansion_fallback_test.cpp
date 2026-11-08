// Інтеграційні тести fallback розширення (TCK-021, TEST-IT-03): реальний клієнт Ollama проти
// контрольованих збоїв, реальний індекс і пошуковий сервіс. Перевіряють FR-021, NFR-008,
// NFR-020: за збою пошук дає ті самі результати, що й без розширення, у межах тайм-ауту.
#include "index/index_manager.hpp"
#include "ollama/generate.hpp"
#include "query/search_service.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using search::ollama::Endpoint;
using search::query::SearchOptions;
using search::query::SearchService;

// Допуск часу (TIMING_TOLERANCE_MS, пропозиція 500 мс; 09 §1).
constexpr std::chrono::milliseconds kTimingTolerance{500};

// Фейковий Ollama: відповідає заданим тілом або мовчить довше тайм-ауту.
class FakeOllama {
  public:
    FakeOllama(std::string reply_body, bool silent) : acceptor_(ioc_, tcp::endpoint(tcp::v4(), 0)) {
        port_ = acceptor_.local_endpoint().port();
        thread_ = std::thread([this, reply_body = std::move(reply_body), silent] {
            boost::system::error_code ec;
            tcp::socket socket(ioc_);
            acceptor_.accept(socket, ec);
            if (ec) {
                return; // acceptor закрито деструктором: клієнта не було
            }
            beast::flat_buffer buffer;
            http::request<http::string_body> req;
            http::read(socket, buffer, req, ec);
            if (ec || silent) {
                if (silent) {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                }
                return;
            }
            http::response<http::string_body> res{http::status::ok, 11};
            res.set(http::field::content_type, "application/json");
            res.body() = reply_body;
            res.prepare_payload();
            http::write(socket, res, ec);
        });
    }

    ~FakeOllama() {
        // Закриття acceptor звільняє accept(), якщо клієнт так і не підключився: тест не зависає.
        boost::system::error_code ignored;
        acceptor_.close(ignored);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::string port() const { return std::to_string(port_); }

  private:
    net::io_context ioc_;
    tcp::acceptor acceptor_;
    unsigned short port_ = 0;
    std::thread thread_;
};

// Порт, на якому ніхто не слухає: з'єднання відхиляється.
std::string closed_port() {
    net::io_context ioc;
    tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), 0));
    const std::string port = std::to_string(acceptor.local_endpoint().port());
    acceptor.close();
    return port;
}

std::vector<std::string> doc_ids(const std::vector<search::query::SearchHit> &hits) {
    std::vector<std::string> ids;
    for (const auto &hit : hits) {
        ids.push_back(hit.doc_id);
    }
    return ids;
}

class ExpansionFallbackTest : public ::testing::Test {
  protected:
    void SetUp() override {
        const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        dataset_ = std::filesystem::temp_directory_path() /
                   ("expansion_fallback_" + name + "_" + std::to_string(getpid()));
        std::filesystem::remove_all(dataset_);
        std::filesystem::create_directories(dataset_);
        { std::ofstream(dataset_ / "a.txt") << "alpha gamma delta\n"; }
        { std::ofstream(dataset_ / "b.txt") << "alpha alpha beta\n"; }
        { std::ofstream(dataset_ / "c.txt") << "unrelated words here\n"; }
        ASSERT_TRUE(manager_.start_build(dataset_, 2, [](const search::index::BuildResult &) {}));
        manager_.wait_for_build();
        search_ = std::make_unique<SearchService>([this] { return manager_.snapshot(); }, dataset_,
                                                  SearchOptions{10, 200});
    }

    void TearDown() override {
        search_.reset();
        std::filesystem::remove_all(dataset_);
    }

    // Виконує розширення так, як сервер: обробник і його клієнт працюють на одному io_context.
    // Повертає терми розширення і фактичний час розширення.
    // Повертає терми, фактичний час і кількість викликів done (FR-021: рівно один раз).
    std::vector<std::string> expand(net::io_context &ioc,
                                    const search::ollama::ExpansionFunction &handler,
                                    const std::string &query, std::chrono::milliseconds *elapsed,
                                    int *calls) {
        std::vector<std::string> terms;
        *calls = 0;
        const auto start = std::chrono::steady_clock::now();
        handler(query, [&terms, calls](std::vector<std::string> result) {
            ++*calls;
            terms = std::move(result);
        });
        ioc.run();
        *elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        return terms;
    }

    // Розширення через реальний обробник з контрольованим Endpoint і тайм-аутом.
    std::vector<std::string> expand_via_ollama(const Endpoint &endpoint,
                                               std::chrono::milliseconds timeout,
                                               const std::string &query,
                                               std::chrono::milliseconds *elapsed, int *calls) {
        net::io_context ioc;
        const auto handler =
            search::ollama::make_expansion_handler(ioc, endpoint, timeout, /*max_terms=*/5);
        return expand(ioc, handler, query, elapsed, calls);
    }

    std::filesystem::path dataset_;
    search::index::IndexManager manager_;
    std::unique_ptr<SearchService> search_;
};

} // namespace

// AC-021.1: Ollama зупинено (з'єднання відхиляється) → терми порожні, done рівно раз,
// результат як без розширення, у межах тайм-ауту + допуск.
TEST_F(ExpansionFallbackTest, StoppedOllamaGivesKeywordResults) {
    const Endpoint endpoint{"127.0.0.1", closed_port(), "tinyllama"};
    const auto timeout = std::chrono::seconds(5);
    std::chrono::milliseconds elapsed{};
    int calls = 0;
    const auto terms = expand_via_ollama(endpoint, timeout, "alpha", &elapsed, &calls);

    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(terms.empty());
    EXPECT_FALSE(search_->search("alpha", terms).empty());
    EXPECT_LE(elapsed, timeout + kTimingTolerance);
}

// AC-021.2: Ollama мовчить довше тайм-ауту → done рівно раз з порожніми термами; завершення
// не раніше тайм-ауту (його викликав таймер) і не пізніше тайм-ауту + допуск (NFR-020).
TEST_F(ExpansionFallbackTest, SilentOllamaTimesOutWithinBoundAndFallsBack) {
    const auto timeout = std::chrono::milliseconds(300);
    FakeOllama fake(R"({"response": "gamma, delta", "done": true})", /*silent=*/true);
    const Endpoint endpoint{"127.0.0.1", fake.port(), "tinyllama"};
    std::chrono::milliseconds elapsed{};
    int calls = 0;
    const auto terms = expand_via_ollama(endpoint, timeout, "alpha", &elapsed, &calls);

    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(terms.empty());
    EXPECT_GE(elapsed, timeout - std::chrono::milliseconds(50));
    EXPECT_LE(elapsed, timeout + kTimingTolerance);
}

// AC-021.3: відповідь без жодного придатного терма (багатослівна фраза) → done рівно раз з
// порожніми термами. Позитивний контроль: відповідь була отримана (інакше тест не відрізнив би
// відмову з'єднання від відкинутої відповіді).
TEST_F(ExpansionFallbackTest, UnusableOllamaReplyFallsBack) {
    FakeOllama fake(R"({"response": "Here are some related terms for you", "done": true})",
                    /*silent=*/false);
    const Endpoint endpoint{"127.0.0.1", fake.port(), "tinyllama"};
    std::chrono::milliseconds elapsed{};
    int calls = 0;
    const auto terms =
        expand_via_ollama(endpoint, std::chrono::seconds(5), "alpha", &elapsed, &calls);

    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(terms.empty());
    EXPECT_LE(elapsed, std::chrono::seconds(5) + kTimingTolerance);
}

// Позитивний контроль успішного шляху: терми з відповіді змінюють результат пошуку. Без нього
// тести вище могли б пройти при обробнику, який завжди повертає порожній список.
TEST_F(ExpansionFallbackTest, SuccessfulExpansionChangesResults) {
    FakeOllama fake(R"({"response": "gamma, delta", "done": true})", /*silent=*/false);
    const Endpoint endpoint{"127.0.0.1", fake.port(), "tinyllama"};
    std::chrono::milliseconds elapsed{};
    int calls = 0;
    const auto terms =
        expand_via_ollama(endpoint, std::chrono::seconds(5), "beta", &elapsed, &calls);

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(terms, (std::vector<std::string>{"gamma", "delta"}));
    // "beta" знаходить лише b.txt; термін "gamma" з розширення додає a.txt.
    const auto baseline = doc_ids(search_->search("beta", {}));
    const auto expanded = doc_ids(search_->search("beta", terms));
    EXPECT_EQ(baseline, (std::vector<std::string>{"b.txt"}));
    EXPECT_NE(std::find(expanded.begin(), expanded.end(), "a.txt"), expanded.end());
}
