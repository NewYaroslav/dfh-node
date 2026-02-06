/**
 * \file bounded_queue.cpp
 * \brief Реализация внутренней bounded-очереди для Task.
 * \details Не содержит синхронизации, рассчитывает на внешний mutex.
 */
#include "internal/bounded_queue.hpp"
#include <utility>

namespace dfh_node {

BoundedQueue::BoundedQueue(std::size_t capacity)
    : m_capacity(capacity) {}

bool BoundedQueue::try_push(Task task) {
    if (m_queue.size() >= m_capacity) {
        m_rejected_count.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    m_queue.push_back(std::move(task));
    m_total_enqueued.fetch_add(1, std::memory_order_relaxed);
    return true;
}

std::optional<Task> BoundedQueue::try_pop() {
    if (m_queue.empty()) {
        return std::nullopt;
    }
    Task task = std::move(m_queue.front());
    m_queue.pop_front();
    return task;
}

std::size_t BoundedQueue::size() const {
    return m_queue.size();
}

std::size_t BoundedQueue::capacity() const {
    return m_capacity;
}

bool BoundedQueue::empty() const {
    return m_queue.empty();
}

bool BoundedQueue::full() const {
    return m_queue.size() >= m_capacity;
}

std::uint64_t BoundedQueue::rejected_count() const {
    return m_rejected_count.load(std::memory_order_relaxed);
}

std::uint64_t BoundedQueue::total_enqueued() const {
    return m_total_enqueued.load(std::memory_order_relaxed);
}

} // namespace dfh_node
