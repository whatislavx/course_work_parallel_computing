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

bool IndexManager::start_build(std::filesystem::path dataset_dir, std::size_t threads) {
    if (threads == 0) {
        throw std::invalid_argument("start_build: потрібен принаймні один потік");
    }
    {
        std::lock_guard lock(mutex_);
        if (building_) {
            return false;
        }
        building_ = true;
        state_ = active_ ? IndexState::Rebuilding : IndexState::Building;
    }
    // Попередня побудова вже завершилася (building_ був false), тож її потік можна приєднати.
    wait_for_build_thread();
    builder_ =
        std::thread([this, dir = std::move(dataset_dir), threads] { run_build(dir, threads); });
    return true;
}

void IndexManager::wait_for_build_thread() {
    if (builder_.joinable()) {
        builder_.join();
    }
}

void IndexManager::wait_for_build() { wait_for_build_thread(); }

void IndexManager::run_build(std::filesystem::path dataset_dir, std::size_t threads) {
    const auto started = std::chrono::steady_clock::now();
    try {
        DiscoveryResult discovery = load_documents(dataset_dir);
        std::unique_ptr<SharedInvertedIndex> built = build_index(discovery.documents, threads);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);

        // Атомарна заміна: один запис під замком, старий знімок живе для поточних пошуків.
        std::lock_guard lock(mutex_);
        active_ = std::shared_ptr<const SharedInvertedIndex>(std::move(built));
        stats_.documents = discovery.documents.size();
        stats_.index_build_time_ms = static_cast<std::uint64_t>(elapsed.count());
        stats_.threads_used = threads;
        last_error_.reset();
        state_ = IndexState::Ready;
        building_ = false;
    } catch (const std::exception &e) {
        std::lock_guard lock(mutex_);
        last_error_ = e.what();
        // Збій не чіпає активний індекс: повертаємося до стану, що був до побудови.
        state_ = active_ ? IndexState::Ready : IndexState::Empty;
        building_ = false;
    }
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
