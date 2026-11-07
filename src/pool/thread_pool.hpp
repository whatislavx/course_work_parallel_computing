#pragma once

#include "pool/task_queue.hpp"

#include <condition_variable>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace search::pool {

// Власний пул потоків (FR-006, FR-036…FR-041, CON-003).
//
// Потокова безпека: усі публічні методи можна викликати з будь-якого потоку, крім shutdown
// з робочого потоку пулу (призвело б до самоочікування) і паралельних shutdown з різних потоків.
// Помилки задач передаються ініціатору через std::future; виняток із задачі не виходить за межі
// робочого потоку.
//
// Стан (guarded by mutex_): queue_, stopping_, pending_. workers_ змінюють лише конструктор
// і shutdown (з одного потоку).
class ThreadPool {
  public:
    // Кидає std::invalid_argument, якщо thread_count == 0 або queue_capacity == 0.
    ThreadPool(std::size_t thread_count, std::size_t queue_capacity);
    ~ThreadPool();

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    // Ставить задачу в чергу. Блокується, якщо черга повна. Кидає std::runtime_error, якщо пул
    // зупиняється (у т.ч. коли submit уже чекав на місце в черзі). Виняток задачі
    // повертається через future::get().
    template <typename F> auto submit(F &&f) -> std::future<std::invoke_result_t<F>> {
        using Result = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(f));
        std::future<Result> result = task->get_future();
        enqueue([task] { (*task)(); });
        return result;
    }

    // Чекає, доки всі прийняті задачі (у черзі та виконувані) не завершаться.
    void wait_idle();

    // Перестає приймати задачі, виконує вже прийняті і приєднує потоки. Ідемпотентний.
    void shutdown();

    std::size_t thread_count() const { return workers_.size(); }

  private:
    void enqueue(Task task);
    void worker_loop();

    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::condition_variable idle_;
    TaskQueue queue_;
    bool stopping_ = false;
    std::size_t pending_ = 0;
    std::vector<std::thread> workers_;
};

} // namespace search::pool
