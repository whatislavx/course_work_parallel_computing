#pragma once

#include "index/shared_inverted_index.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace search::index {

// Стани життєвого циклу індексу (FR-008, OQ-08…OQ-10).
//   Empty      — жодної успішної побудови;
//   Building   — перша побудова триває, готового індексу немає;
//   Ready      — активний індекс доступний, побудови немає;
//   Rebuilding — активний індекс доступний, триває побудова наступного.
enum class IndexState { Empty, Building, Ready, Rebuilding };

// Статистика, що відповідає активному індексу (FR-010, FR-011).
struct IndexStats {
    std::size_t documents = 0;
    std::uint64_t index_build_time_ms = 0;
    std::size_t threads_used = 0;
};

// Керує побудовою і заміною індексу. Побудова виконується у власному потоці менеджера,
// який приєднується в деструкторі або в wait_for_build (без відокремлених потоків).
//
// Потокова безпека: усі методи можна викликати з будь-якого потоку, крім того, що
// start_build і wait_for_build мають викликатися з одного потоку (керування потоком побудови).
// Guarded by mutex_: state_, active_, stats_, last_error_, building_.
class IndexManager {
  public:
    IndexManager() = default;
    ~IndexManager();

    IndexManager(const IndexManager &) = delete;
    IndexManager &operator=(const IndexManager &) = delete;

    // Запускає побудову з каталогу dataset_dir і threads потоками. Повертає false, якщо
    // побудова вже триває (відповідає 409 index_build_in_progress). Кидає std::invalid_argument
    // при threads == 0.
    bool start_build(std::filesystem::path dataset_dir, std::size_t threads);

    // Блокується до завершення поточної побудови (якщо вона є).
    void wait_for_build();

    IndexState state() const;

    // Знімок активного індексу; nullptr до першої успішної побудови (пошук → 409 index_not_ready).
    // Знімок лишається дійсним, навіть якщо індекс замінили (OQ-10).
    std::shared_ptr<const SharedInvertedIndex> snapshot() const;

    // Узгоджений знімок статистики активного індексу; нулі до першої побудови.
    IndexStats stats() const;

    // Текст останньої помилки побудови, якщо остання побудова завершилася збоєм.
    std::optional<std::string> last_error() const;

  private:
    void run_build(std::filesystem::path dataset_dir, std::size_t threads);
    void wait_for_build_thread();

    mutable std::mutex mutex_;
    IndexState state_ = IndexState::Empty;
    std::shared_ptr<const SharedInvertedIndex> active_;
    IndexStats stats_;
    std::optional<std::string> last_error_;
    bool building_ = false;
    std::thread builder_;
};

} // namespace search::index
