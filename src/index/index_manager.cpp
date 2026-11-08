#include "index/index_manager.hpp"

#include "index/document_source.hpp"
#include "index/index_builder.hpp"

#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

namespace search::index {

IndexManager::~IndexManager() { wait_for_build_thread(); }

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
        building_ = true;
        state_ = active_ ? IndexState::Rebuilding : IndexState::Building;
        // Потік попередньої побудови вже завершив роботу (building_ був false), його лише
        // приєднуємо. builder_ змінюється тільки під замком.
        previous = std::move(builder_);
        try {
            builder_ = std::thread(
                [this, dir = std::move(dataset_dir), threads, done = std::move(on_done)]() mutable {
                    run_build(dir, threads, std::move(done));
                });
        } catch (...) {
            // Не вдалося створити потік: скидаємо стан, щоб не застрягнути в Building.
            building_ = false;
            state_ = active_ ? IndexState::Ready : IndexState::Empty;
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

void IndexManager::wait_for_build() { wait_for_build_thread(); }

void IndexManager::run_build(std::filesystem::path dataset_dir, std::size_t threads,
                             BuildCallback on_done) {
    const auto started = std::chrono::steady_clock::now();
    BuildResult result;
    try {
        DiscoveryResult discovery = load_documents(dataset_dir);
        std::unique_ptr<SharedInvertedIndex> built = build_index(discovery.documents, threads);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);

        result.ok = true;
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
    } catch (const DatasetError &e) {
        result.error_code = "dataset_unavailable";
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
    if (on_done) {
        on_done(result);
    }
}

void IndexManager::finish_build(const BuildResult &result) {
    std::lock_guard lock(mutex_);
    last_error_ = result.error_message;
    // Збій не чіпає активний індекс: повертаємося до стану, що був до побудови.
    state_ = active_ ? IndexState::Ready : IndexState::Empty;
    building_ = false;
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
