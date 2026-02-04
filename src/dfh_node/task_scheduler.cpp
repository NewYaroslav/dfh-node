/**
 * \file task_scheduler.cpp
 * \brief Реализация приоритетного планировщика задач.
 * \details Включает stop-now shutdown и базовый сбор метрик очередей.
 */
#include "dfh_node/task_scheduler.hpp"

#include <utility>

namespace dfh_node {

TaskScheduler::TaskScheduler(std::size_t ingest_capacity, std::size_t history_capacity)
    : ingest_queue_(ingest_capacity)
    , history_queue_(history_capacity) {}

EnqueueResult TaskScheduler::enqueue_ingest(Job job) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Почему lock здесь: единственная точка синхронизации для обеих очередей,
    // чтобы исключить гонки и missed wake-up между enqueue и pop.
    if (!ingest_queue_.try_push(std::move(job))) {
        return {EnqueueStatus::Rejected, "overload.ingest_queue_full", "Ingest queue is full"};
    }
    // Пробуждаем только одного воркера: одной новой задачи достаточно для одного потока.
    cv_.notify_one();
    return {EnqueueStatus::Ok, "", ""};
}

EnqueueResult TaskScheduler::enqueue_history(Job job) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Единый mutex для обеих очередей гарантирует корректный приоритет в pop_next_job().
    if (!history_queue_.try_push(std::move(job))) {
        return {EnqueueStatus::Rejected, "overload.history_queue_full", "History queue is full"};
    }
    cv_.notify_one();
    return {EnqueueStatus::Ok, "", ""};
}

std::optional<Job> TaskScheduler::pop_next_job() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        // Проверяем shutdown первым: stop-now семантика важнее любых задач.
        if (shutdown_flag_.load(std::memory_order_relaxed)) {
            return std::nullopt;
        }

        // Приоритет ingest: история обслуживается только при отсутствии ingest.
        if (!ingest_queue_.empty()) {
            return ingest_queue_.try_pop();
        }

        // History обслуживается как второй приоритет.
        if (!history_queue_.empty()) {
            return history_queue_.try_pop();
        }

        // TODO: starvation guard для history (consecutive_ingest_count, force 1 history after N ingest).
        // Используем cv_.wait без polling: избегаем активного ожидания и лишней нагрузки.
        cv_.wait(lock);
    }
}

void TaskScheduler::shutdown() {
    // TODO: опциональный drain mode (дождаться пустых очередей) — отдельная фича, не в Этапе 3.
    // stop-now: воркеры должны выйти сразу после пробуждения, даже если в очередях есть задачи.
    shutdown_flag_.store(true, std::memory_order_relaxed);
    cv_.notify_all();
}

QueueMetrics TaskScheduler::get_ingest_metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Метрики очереди читаем под тем же mutex, что и операции с очередью.
    QueueMetrics metrics;
    metrics.current_size = ingest_queue_.size();
    metrics.capacity = ingest_queue_.capacity();
    metrics.rejected_count = ingest_queue_.rejected_count();
    metrics.dropped_count = 0;
    metrics.total_enqueued = ingest_queue_.total_enqueued();
    metrics.total_processed = 0;
    metrics.avg_wait_ms = 0.0;
    return metrics;
}

QueueMetrics TaskScheduler::get_history_metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Аналогично ingest: единый mutex сохраняет согласованность снимка метрик.
    QueueMetrics metrics;
    metrics.current_size = history_queue_.size();
    metrics.capacity = history_queue_.capacity();
    metrics.rejected_count = history_queue_.rejected_count();
    metrics.dropped_count = 0;
    metrics.total_enqueued = history_queue_.total_enqueued();
    metrics.total_processed = 0;
    metrics.avg_wait_ms = 0.0;
    return metrics;
}

} // namespace dfh_node
