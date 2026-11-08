#include "index/index_manager.hpp"

#include "index/document_source.hpp"
#include "index/index_builder.hpp"

#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace search::index {

IndexManager::~IndexManager() {
    request_cancel();
    wait_for_build_thread();
}

void IndexManager::request_cancel() { cancel_requested_.store(true); }

bool IndexManager::start_build(std::filesystem::path dataset_dir, std::size_t threads,
                               BuildCallback on_done) {
    if (threads == 0) {
        throw std::invalid_argument("start_build: потрібен принаймні один потік");
    }
    std::thread previous;
    {
        std::lock_guard lock(mutex_);
        if (building_) {
            return false;
        }
        // Виклик із власного on_done: приєднання потоку самого себе призвело б до помилки.
        if (builder_.joinable() && builder_.get_id() == std::this_thread::get_id()) {
            throw std::logic_error("start_build: виклик із callback побудови заборонено");
        }
        building_ = true;
        state_ = active_ ? IndexState::Rebuilding : IndexState::Building;
        // Потік попередньої побудови вже вийшов з побудови (building_ був false); його
        // приєднуємо поза замком. builder_ змінюється тільки під замком.
        previous = std::move(builder_);
        try {
            builder_ = std::thread(
                [this, dir = std::move(dataset_dir), threads, done = std::move(on_done)]() mutable {
                    run_build(dir, threads, std::move(done));
                });
        } catch (...) {
            // Не вдалося створити потік: повертаємо попередній потік і стан, щоб не
            // застрягнути в Building і не втратити потік для приєднання.
            builder_ = std::move(previous);
            building_ = false;
            state_ = active_ ? IndexState::Ready : IndexState::Empty;
            idle_cv_.notify_all();
            throw;
        }
    }
    if (previous.joinable()) {
        previous.join();
    }
    return true;
}

void IndexManager::wait_for_build_thread() {
    std::thread current;
    {
        std::lock_guard lock(mutex_);
        current = std::move(builder_);
    }
    if (current.joinable()) {
        current.join();
    }
}

void IndexManager::wait_for_build() {
    std::unique_lock lock(mutex_);
    idle_cv_.wait(lock, [this] { return !building_; });
}

void IndexManager::run_build(std::filesystem::path dataset_dir, std::size_t threads,
                             BuildCallback on_done) {
    const auto started = std::chrono::steady_clock::now();
    BuildResult result;
    try {
        DiscoveryResult discovery = load_documents(dataset_dir);
        std::unique_ptr<SharedInvertedIndex> built =
            build_index(discovery.documents, threads, tokenize, &cancel_requested_);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);

        result.stats.documents = discovery.documents.size();
        result.stats.index_build_time_ms = static_cast<std::uint64_t>(elapsed.count());
        result.stats.threads_used = threads;

        // Атомарна заміна: один запис під замком, старий знімок живе для поточних пошуків.
        std::shared_ptr<const SharedInvertedIndex> fresh(std::move(built));
        std::lock_guard lock(mutex_);
        active_ = std::move(fresh);
        stats_ = result.stats;
        last_error_.reset();
        state_ = IndexState::Ready;
        building_ = false;
        result.ok = true;
        idle_cv_.notify_all();
    } catch (const DatasetError &e) {
        result.error_code = "dataset_unavailable";
        result.error_message = e.what();
        finish_build(result);
    } catch (const BuildCancelledError &e) {
        result.error_code = "build_cancelled";
        result.error_message = e.what();
        finish_build(result);
    } catch (const IndexBuildError &e) {
        result.error_code = "index_build_failed";
        result.error_message = e.what();
        finish_build(result);
    } catch (const std::exception &e) {
        result.error_code = "internal_error";
        result.error_message = e.what();
        finish_build(result);
    } catch (...) {
        // Виняток не має вийти з робочого потоку (std::terminate).
        result.error_code = "internal_error";
        result.error_message = "unknown error";
        finish_build(result);
    }
    // Виняток з callback не має вийти з робочого потоку (std::terminate).
    if (on_done) {
        try {
            on_done(result);
        } catch (...) {
            // Немає куди повідомити: відповідь клієнту вже не можна надіслати.
        }
    }
}

void IndexManager::finish_build(const BuildResult &result) {
    std::lock_guard lock(mutex_);
    // Стан скидаємо першим: якщо присвоєння рядка кине виняток, building_ не лишиться true.
    // Збій не чіпає активний індекс: повертаємося до стану, що був до побудови.
    state_ = active_ ? IndexState::Ready : IndexState::Empty;
    building_ = false;
    idle_cv_.notify_all();
    last_error_ = result.error_message;
}

IndexState IndexManager::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

std::shared_ptr<const SharedInvertedIndex> IndexManager::snapshot() const {
    std::lock_guard lock(mutex_);
    return active_;
}

IndexStats IndexManager::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

std::optional<std::string> IndexManager::last_error() const {
    std::lock_guard lock(mutex_);
    return last_error_;
}

} // namespace search::index
