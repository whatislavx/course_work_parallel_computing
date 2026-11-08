#pragma once

#include "index/shared_inverted_index.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
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

// Результат завершеної побудови, що передається в callback.
// error_code — код з 06 §2.4: "dataset_unavailable", "index_build_failed" або "internal_error".
struct BuildResult {
    bool ok = false;
    std::string error_code;
    std::string error_message;
    IndexStats stats;
};

// Викликається з потоку побудови після оновлення стану (без утримання замка).
using BuildCallback = std::function<void(const BuildResult &)>;

// Керує побудовою і заміною індексу. Побудова виконується у власному потоці менеджера,
// який приєднується в деструкторі або в wait_for_build (без відокремлених потоків).
//
// Потокова безпека: усі методи можна викликати з будь-якого потоку. Guarded by mutex_:
// state_, active_, stats_, last_error_, building_, builder_.
// Відоме обмеження: побудова не скасовується. Деструктор чекає її завершення, тож зупинка
// процесу під час побудови триває до кінця побудови (узгоджено, без cooperative stop-flag).
class IndexManager {
  public:
    IndexManager() = default;
    ~IndexManager();

    IndexManager(const IndexManager &) = delete;
    IndexManager &operator=(const IndexManager &) = delete;

    // Запускає побудову з каталогу dataset_dir і threads потоками. Повертає false, якщо
    // побудова вже триває (409 index_build_in_progress); тоді on_done не викликається.
    // Кидає std::invalid_argument при threads == 0, std::logic_error при виклику з on_done,
    // std::system_error, якщо не вдалося створити потік (стан повертається до попереднього).
    // on_done (за наявності) викликається рівно один раз після завершення, з потоку побудови.
    bool start_build(std::filesystem::path dataset_dir, std::size_t threads,
                     BuildCallback on_done = {});

    // Блокується, доки побудова не завершиться (стан building_ скинуто). Callback on_done може
    // ще виконуватися. Не викликати з on_done: callback виконується в потоці побудови.
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
    void run_build(std::filesystem::path dataset_dir, std::size_t threads, BuildCallback on_done);
    void finish_build(const BuildResult &result);
    void wait_for_build_thread();

    mutable std::mutex mutex_;
    IndexState state_ = IndexState::Empty;
    std::shared_ptr<const SharedInvertedIndex> active_;
    IndexStats stats_;
    std::optional<std::string> last_error_;
    bool building_ = false;
    std::condition_variable idle_cv_; // сигналізує про building_ == false
    std::thread builder_;
};

} // namespace search::index
