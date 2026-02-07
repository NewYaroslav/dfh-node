/**
 * \file task_scheduler.cpp
 * \brief Реализация приоритетного планировщика задач.
 * \details Включает stop-now shutdown и базовый сбор метрик очередей.
 */
#include "task_scheduler.hpp"

#include <utility>

namespace dfh_node {

TaskScheduler::TaskScheduler(std::size_t high_capacity,
                             std::size_t low_capacity)
    : m_high_queue(high_capacity), m_low_queue(low_capacity) {}

EnqueueResult TaskScheduler::enqueue_high(Task task) {
    std::lock_guard<std::mutex> lock(m_mutex);
    task.lane = TaskLane::High;
    // Почему lock здесь: единственная точка синхронизации для обеих очередей,
    // чтобы исключить гонки и missed wake-up между enqueue и pop.
    if (!m_high_queue.try_push(std::move(task))) {
        return {EnqueueStatus::Rejected, "overload.high_priority_queue_full",
                "High-priority queue is full"};
    }
    // Пробуждаем только одного воркера: одной новой задачи достаточно для
    // одного потока.
    m_cv.notify_one();
    return {EnqueueStatus::Ok, "", ""};
}

EnqueueResult TaskScheduler::enqueue_low(Task task) {
    std::lock_guard<std::mutex> lock(m_mutex);
    task.lane = TaskLane::Low;
    // Единый mutex для обеих очередей гарантирует корректный приоритет в
    // pop_next_task().
    if (!m_low_queue.try_push(std::move(task))) {
        return {EnqueueStatus::Rejected, "overload.low_priority_queue_full",
                "Low-priority queue is full"};
    }
    m_cv.notify_one();
    return {EnqueueStatus::Ok, "", ""};
}

std::optional<Task> TaskScheduler::pop_next_task() {
    std::unique_lock<std::mutex> lock(m_mutex);
    while (true) {
        // Проверяем shutdown первым: stop-now семантика важнее любых задач.
        if (m_shutdown_flag.load(std::memory_order_relaxed)) {
            return std::nullopt;
        }

        // Приоритет high: low обслуживается только при отсутствии high.
        if (!m_high_queue.empty()) {
            return m_high_queue.try_pop();
        }

        // Low обслуживается как второй приоритет.
        if (!m_low_queue.empty()) {
            return m_low_queue.try_pop();
        }

        // TODO: starvation guard для low (consecutive_high_count, force 1 low
        // after N high). Используем m_cv.wait без polling: избегаем активного
        // ожидания и лишней нагрузки.
        m_cv.wait(lock);
    }
}

void TaskScheduler::shutdown() {
    // TODO: опциональный drain mode (дождаться пустых очередей) — отдельная
    // фича, не в Этапе 3. stop-now: воркеры должны выйти сразу после
    // пробуждения, даже если в очередях есть задачи.
    m_shutdown_flag.store(true, std::memory_order_relaxed);
    m_cv.notify_all();
}

QueueMetrics TaskScheduler::high_metrics() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Метрики очереди читаем под тем же mutex, что и операции с очередью.
    QueueMetrics metrics;
    metrics.current_size = m_high_queue.size();
    metrics.capacity = m_high_queue.capacity();
    metrics.rejected_count = m_high_queue.rejected_count();
    metrics.dropped_count = 0;
    metrics.total_enqueued = m_high_queue.total_enqueued();
    metrics.total_processed = 0;
    metrics.avg_wait_ms = 0.0;
    return metrics;
}

QueueMetrics TaskScheduler::low_metrics() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Аналогично high: единый mutex сохраняет согласованность снимка метрик.
    QueueMetrics metrics;
    metrics.current_size = m_low_queue.size();
    metrics.capacity = m_low_queue.capacity();
    metrics.rejected_count = m_low_queue.rejected_count();
    metrics.dropped_count = 0;
    metrics.total_enqueued = m_low_queue.total_enqueued();
    metrics.total_processed = 0;
    metrics.avg_wait_ms = 0.0;
    return metrics;
}

} // namespace dfh_node
