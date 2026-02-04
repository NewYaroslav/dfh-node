/**
 * \file task_scheduler.hpp
 * \brief Приоритетный планировщик задач и метрики очередей.
 * \details Определяет публичный API TaskScheduler для ingest/history очередей.
 */
#pragma once

#include "dfh_node/job.hpp"
#include "dfh_node/status.hpp"
#include "dfh_node/internal/bounded_queue.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>

namespace dfh_node {

/// \brief Приоритетный планировщик задач (ingest > history).
///
/// Единая точка enqueue для обеих очередей, владеет единым mutex + cv.
/// pop_next_job() реализует приоритет: сначала ingest, затем history.
/// Shutdown семантика: stop-now (не drain, jobs могут остаться необработанными).
class TaskScheduler {
public:
    /// \brief Конструктор.
    /// \param ingest_capacity Вместимость очереди записи.
    /// \param history_capacity Вместимость очереди чтения.
    TaskScheduler(std::size_t ingest_capacity, std::size_t history_capacity);

    /// \brief Поставить задачу в очередь записи.
    /// \param job Задача (move).
    /// \return EnqueueResult с status=Ok/Rejected.
    EnqueueResult enqueue_ingest(Job job);

    /// \brief Поставить задачу в очередь чтения.
    /// \param job Задача (move).
    /// \return EnqueueResult с status=Ok/Rejected.
    EnqueueResult enqueue_history(Job job);

    /// \brief Извлечь следующую задачу (blocking, приоритет ingest > history).
    ///
    /// Блокирует вызывающий поток, если обе очереди пусты.
    /// Возвращает std::nullopt только при shutdown.
    /// \return Job или std::nullopt при shutdown.
    std::optional<Job> pop_next_job();

    /// \brief Остановить планировщик (stop-now, не drain).
    ///
    /// Устанавливает shutdown_flag и пробуждает все ожидающие потоки.
    /// НЕ дожидается обработки оставшихся задач в очередях.
    void shutdown();

    /// \brief Получить метрики очереди записи.
    /// \return Снимок метрик ingest-очереди.
    QueueMetrics get_ingest_metrics() const;

    /// \brief Получить метрики очереди чтения.
    /// \return Снимок метрик history-очереди.
    QueueMetrics get_history_metrics() const;

private:
    BoundedQueue ingest_queue_; ///< Очередь записи (БЕЗ собственного mutex/cv).
    BoundedQueue history_queue_; ///< Очередь чтения (БЕЗ собственного mutex/cv).
    mutable std::mutex mutex_; ///< ЕДИНЫЙ mutex для обеих очередей.
    std::condition_variable cv_; ///< ЕДИНЫЙ notifier для воркеров.
    std::atomic<bool> shutdown_flag_{false}; ///< Флаг остановки.
};

} // namespace dfh_node
