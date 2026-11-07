#include "pool/task_queue.hpp"

#include <utility>

namespace search::pool {

TaskQueue::TaskQueue(std::size_t capacity) : slots_(capacity) {}

void TaskQueue::push(Task task) {
    slots_[(head_ + size_) % slots_.size()] = std::move(task);
    ++size_;
}

Task TaskQueue::pop() {
    Task task = std::move(slots_[head_]);
    slots_[head_] = Task{};
    head_ = (head_ + 1) % slots_.size();
    --size_;
    return task;
}

} // namespace search::pool
