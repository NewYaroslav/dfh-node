/**
 * \file bounded_queue.hpp
 * \brief Внутренняя bounded-очередь без синхронизации.
 * \details Используется TaskScheduler при внешнем удержании lock.
 */
#pragma once

#include "dfh_node/job.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace dfh_node {

/// \brief Bounded контейнер для Job (internal API, НЕ thread-safe).
///
/// КРИТИЧНО: НЕ добавлять mutex/cv/shutdown — это нарушит архитектуру и создаст race (missed wake-up).
/// Вся синхронизация в TaskScheduler (единый mutex + cv для обеих очередей).
/// Caller ДОЛЖЕН держать lock при вызове try_push/try_pop.
class BoundedQueue {
public:
    /// \brief Конструктор.
    /// \param capacity Максимальная вместимость очереди.
    explicit BoundedQueue(std::size_t capacity);

    /// \brief Попытка добавить задачу в очередь (caller держит lock).
    /// \param job Задача для добавления (move).
    /// \return true если успешно, false если очередь полна (rejected_count инкрементируется).
    bool try_push(Job job);

    /// \brief Попытка извлечь задачу из очереди (caller держит lock).
    /// \return Job если очередь не пуста, nullopt если пуста.
    std::optional<Job> try_pop();

    /// \brief Текущий размер очереди (O(1)).
    std::size_t size() const;

    /// \brief Максимальная вместимость.
    std::size_t capacity() const;

    /// \brief Проверка пустоты.
    bool empty() const;

    /// \brief Проверка заполненности.
    bool full() const;

    /// \brief Количество отклонённых задач (atomic, thread-safe).
    std::uint64_t rejected_count() const;

    /// \brief Всего задач поставлено в очередь (atomic, thread-safe).
    std::uint64_t total_enqueued() const;

private:
    std::deque<Job> queue_; ///< Очередь задач (FIFO).
    std::size_t capacity_; ///< Максимальная вместимость.
    std::atomic<std::uint64_t> rejected_count_{0}; ///< Счётчик отклонённых задач.
    std::atomic<std::uint64_t> total_enqueued_{0}; ///< Счётчик поставленных в очередь задач.
};

} // namespace dfh_node
