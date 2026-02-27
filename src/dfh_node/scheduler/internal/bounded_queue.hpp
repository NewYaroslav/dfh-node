/// \file bounded_queue.hpp
/// \brief Внутренняя ограниченная очередь без синхронизации.
/// \details Используется `TaskScheduler` при внешнем удержании блокировки.
///
#pragma once

#include "core/task.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace dfh_node {

/// \brief Ограниченный контейнер для `Task` (внутренний API, НЕ thread-safe).
///
/// КРИТИЧНО: НЕ добавлять `mutex`/`cv`/`shutdown` — это нарушит архитектуру и
/// создаст гонки (включая пропущенные пробуждения). Вся синхронизация в
/// `TaskScheduler` (единый `mutex` + `cv` для обеих очередей). Вызывающая
/// сторона обязана держать блокировку при вызове `try_push`/`try_pop`.
class BoundedQueue {
public:
    /// \brief Конструктор.
    /// \param capacity Максимальная вместимость очереди.
    explicit BoundedQueue(std::size_t capacity);

    /// \brief Попытка добавить задачу в очередь (вызывающая сторона держит блокировку).
    /// \param task Задача для добавления (перемещается).
    /// \return true если успешно, false если очередь полна (`rejected_count`
    /// инкрементируется).
    bool try_push(Task task);

    /// \brief Попытка извлечь задачу из очереди (вызывающая сторона держит блокировку).
    /// \return `Task`, если очередь не пуста, `nullopt`, если пуста.
    std::optional<Task> try_pop();

    /// \brief Текущий размер очереди (O(1)).
    std::size_t size() const;

    /// \brief Максимальная вместимость.
    std::size_t capacity() const;

    /// \brief Проверка пустоты.
    bool empty() const;

    /// \brief Проверка заполненности.
    bool full() const;

    /// \brief Количество отклонённых задач (`atomic`, thread-safe).
    std::uint64_t rejected_count() const;

    /// \brief Всего задач поставлено в очередь (`atomic`, thread-safe).
    std::uint64_t total_enqueued() const;

private:
    std::deque<Task> m_queue;                       ///< Очередь задач (FIFO).
    std::size_t m_capacity;                         ///< Максимальная вместимость.
    std::atomic<std::uint64_t> m_rejected_count{0}; ///< Счётчик отклонённых задач.
    std::atomic<std::uint64_t> m_total_enqueued{0}; ///< Счётчик поставленных в очередь задач.
};

} // namespace dfh_node
