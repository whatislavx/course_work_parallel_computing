#pragma once

#include <cstddef>
#include <functional>
#include <vector>

namespace search::pool {

// Задача пулу. Копіюється у черзі, тому обгортка над packaged_task живе у shared_ptr.
using Task = std::function<void()>;

// Кільцева черга фіксованої місткості поверх std::vector (AD-02, CON-015).
// Не потокобезпечна: синхронізує ThreadPool (guarded by ThreadPool::mutex_).
class TaskQueue {
  public:
    explicit TaskQueue(std::size_t capacity);

    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == slots_.size(); }
    std::size_t size() const { return size_; }

    // Попередня умова: !full().
    void push(Task task);
    // Попередня умова: !empty().
    Task pop();

  private:
    std::vector<Task> slots_;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
};

} // namespace search::pool
