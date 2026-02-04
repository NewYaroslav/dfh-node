#include "bounded_queue.hpp"
#include <utility>

namespace dfh_node {

BoundedQueue::BoundedQueue(std::size_t capacity)
    : capacity_(capacity) {}

bool BoundedQueue::try_push(Job job) {
    if (queue_.size() >= capacity_) {
        rejected_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    queue_.push_back(std::move(job));
    total_enqueued_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

std::optional<Job> BoundedQueue::try_pop() {
    if (queue_.empty()) {
        return std::nullopt;
    }
    Job job = std::move(queue_.front());
    queue_.pop_front();
    return job;
}

std::size_t BoundedQueue::size() const {
    return queue_.size();
}

std::size_t BoundedQueue::capacity() const {
    return capacity_;
}

bool BoundedQueue::empty() const {
    return queue_.empty();
}

bool BoundedQueue::full() const {
    return queue_.size() >= capacity_;
}

std::uint64_t BoundedQueue::rejected_count() const {
    return rejected_count_.load(std::memory_order_relaxed);
}

std::uint64_t BoundedQueue::total_enqueued() const {
    return total_enqueued_.load(std::memory_order_relaxed);
}

} // namespace dfh_node
