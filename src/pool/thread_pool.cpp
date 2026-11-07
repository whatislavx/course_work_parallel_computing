#include "pool/thread_pool.hpp"

#include <exception>

namespace search::pool {

ThreadPool::ThreadPool(std::size_t thread_count, std::size_t queue_capacity)
    : queue_(queue_capacity) {
    if (thread_count == 0) {
        throw std::invalid_argument("ThreadPool: потрібен принаймні один потік");
    }
    if (queue_capacity == 0) {
        throw std::invalid_argument("ThreadPool: місткість черги має бути більшою за нуль");
    }
    workers_.reserve(thread_count);
    try {
        for (std::size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    } catch (...) {
        // Частково створені потоки мають бути приєднані до виходу з конструктора.
        shutdown();
        throw;
    }
}

ThreadPool::~ThreadPool() { shutdown(); }

void ThreadPool::enqueue(Task task) {
    std::unique_lock lock(mutex_);
    not_full_.wait(lock, [this] { return stopping_ || !queue_.full(); });
    if (stopping_) {
        throw std::runtime_error("ThreadPool: пул зупинено, задача відхилена");
    }
    queue_.push(std::move(task));
    ++pending_;
    lock.unlock();
    not_empty_.notify_one();
}

void ThreadPool::worker_loop() {
    for (;;) {
        Task task;
        {
            std::unique_lock lock(mutex_);
            not_empty_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                return; // зупинка і черга порожня: виходимо
            }
            task = queue_.pop();
        }
        not_full_.notify_one();

        try {
            task();
        } catch (...) {
            // Задачі з submit загортаються в packaged_task, який сам зберігає виняток.
            // Цей блок захищає потік від невідомих винятків з інших джерел.
        }

        std::lock_guard lock(mutex_);
        --pending_;
        if (pending_ == 0) {
            idle_.notify_all();
        }
    }
}

void ThreadPool::wait_idle() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return pending_ == 0; });
}

void ThreadPool::shutdown() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    not_empty_.notify_all();
    not_full_.notify_all();
    for (std::thread &worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

} // namespace search::pool
