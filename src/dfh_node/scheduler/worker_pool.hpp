/// \file worker_pool.hpp
/// \brief Пул воркеров для выполнения задач из TaskScheduler.
/// \details Содержит API для запуска воркеров и получения метрик обработки.
///
#pragma once

#include "task_scheduler.hpp"
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace dfh_node {

/// \brief Пул воркеров для обработки задач.
///
/// Воркеры получают задачи из TaskScheduler через pop_next_task().
/// Метрики обработки: total_processed, total_wait_ms, avg_wait_ms (по лейну).
/// остановка семантика: мягкая остановка = join потоков без зависаний (не drain).
class WorkerPool {
public:
    /// \brief Конструктор.
    /// \param num_workers Количество воркеров.
    /// \param scheduler Ссылка на TaskScheduler.
    WorkerPool(std::size_t num_workers, TaskScheduler &scheduler);

    /// \brief Деструктор (вызывает остановка, если не был вызван вручную).
    ~WorkerPool();

    /// \brief Запустить воркеры.
    void start();

    /// \brief Остановить воркеры (мягкая остановка = join без зависаний).
    ///
    /// Вызывает scheduler.остановка() и ждёт завершения всех потоков.
    void shutdown();

    /// \brief Возвращает true, если пул воркеров запущен.
    /// \return true после `start()` и до завершения `shutdown()`.
    bool is_running() const;

    /// \brief Всего задач обработано (по лейну планировщика).
    /// \param lane Лейн планировщика (High/Low).
    std::uint64_t total_processed(TaskLane lane) const;

    /// \brief Суммарное время ожидания в очереди (мс, по лейну планировщика).
    /// \param lane Лейн планировщика.
    std::uint64_t total_wait_ms(TaskLane lane) const;

    /// \brief Среднее время ожидания в очереди (мс, по лейну планировщика).
    /// \param lane Лейн планировщика.
    /// \return avg_wait_ms = total_wait_ms / total_processed (или 0.0 если
    /// processed == 0).
    double avg_wait_ms(TaskLane lane) const;

private:
    /// \brief Рабочий цикл воркера.
    void worker_loop();

    TaskScheduler &m_scheduler;                            ///< Ссылка на планировщик.
    std::vector<std::thread> m_workers;                    ///< Пул потоков.
    std::atomic<bool> m_running{false};                    ///< Признак запущенного пула.
    std::atomic<std::uint64_t> m_total_processed[2]{0, 0}; ///< [High, Low] (C++17: fetch_add).
    std::atomic<std::uint64_t> m_total_wait_ms[2]{0, 0};   ///< [High, Low].
};

} // namespace dfh_node
