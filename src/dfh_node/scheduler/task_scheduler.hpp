/// \file task_scheduler.hpp
/// \brief Приоритетный планировщик задач и метрики очередей.
/// \details Определяет публичный API `TaskScheduler` для high/low-приоритетных очередей.

#pragma once

#include "core/status.hpp"
#include "core/task.hpp"
#include "internal/bounded_queue.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>

namespace dfh_node {

/// \brief Приоритетный планировщик задач (`high` > `low`).
///
/// Единая точка постановки задач для обеих очередей, владеет единым `mutex` + `cv`.
/// `pop_next_task()` реализует приоритет: сначала `high`, затем `low`.
/// Семантика остановки: `stop-now` (без `drain`, задачи могут остаться
/// необработанными).
class TaskScheduler {
public:
    /// \brief Конструктор.
    /// \param high_capacity Вместимость high-priority очереди.
    /// \param low_capacity Вместимость low-priority очереди.
    TaskScheduler(std::size_t high_capacity, std::size_t low_capacity);

    /// \brief Поставить задачу в high-priority очередь.
    /// \param task Задача (перемещается).
    /// \return `EnqueueResult` со статусом `Ok`/`Rejected`.
    EnqueueResult enqueue_high(Task task);

    /// \brief Поставить задачу в low-priority очередь.
    /// \param task Задача (перемещается).
    /// \return `EnqueueResult` со статусом `Ok`/`Rejected`.
    EnqueueResult enqueue_low(Task task);

    /// \brief Извлечь следующую задачу (блокирующий вызов, приоритет `high` > `low`).
    ///
    /// Блокирует вызывающий поток, если обе очереди пусты.
    /// Возвращает `std::nullopt` только при остановке.
    /// \return `Task` или `std::nullopt` при остановке.
    std::optional<Task> pop_next_task();

    /// \brief Остановить планировщик (`stop-now`, без `drain`).
    ///
    /// Устанавливает `m_shutdown_flag` и пробуждает все ожидающие потоки.
    /// НЕ дожидается обработки оставшихся задач в очередях.
    void shutdown();

    /// \brief Получить метрики high-priority очереди.
    /// \return Снимок метрик high-priority очереди.
    QueueMetrics high_metrics() const;

    /// \brief Получить метрики low-priority очереди.
    /// \return Снимок метрик low-priority очереди.
    QueueMetrics low_metrics() const;

private:
    BoundedQueue m_high_queue;                ///< High-priority очередь (без собственного `mutex`/`cv`).
    BoundedQueue m_low_queue;                 ///< Low-priority очередь (без собственного `mutex`/`cv`).
    mutable std::mutex m_mutex;               ///< Единый `mutex` для обеих очередей.
    std::condition_variable m_cv;             ///< Единый `notifier` для воркеров.
    std::atomic<bool> m_shutdown_flag{false}; ///< Флаг остановки.
};

} // namespace dfh_node
