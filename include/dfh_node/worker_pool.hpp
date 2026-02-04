/**
 * \file worker_pool.hpp
 * \brief Пул воркеров для выполнения задач из TaskScheduler.
 * \details Содержит API для запуска воркеров и получения метрик обработки.
 */
#pragma once

#include "dfh_node/task_scheduler.hpp"
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace dfh_node {

/// \brief Пул воркеров для обработки задач.
///
/// Воркеры получают задачи из TaskScheduler через pop_next_job().
/// Метрики обработки: total_processed, total_wait_ms, avg_wait_ms (per-kind).
/// Shutdown семантика: graceful = join потоков без зависаний (не drain).
class WorkerPool {
public:
    /// \brief Конструктор.
    /// \param num_workers Количество воркеров.
    /// \param scheduler Ссылка на TaskScheduler.
    WorkerPool(std::size_t num_workers, TaskScheduler& scheduler);

    /// \brief Деструктор (вызывает shutdown, если не был вызван вручную).
    ~WorkerPool();

    /// \brief Запустить воркеры.
    void start();

    /// \brief Остановить воркеры (graceful = join без зависаний).
    ///
    /// Вызывает scheduler.shutdown() и ждёт завершения всех потоков.
    void shutdown();

    /// \brief Всего задач обработано (по типу).
    /// \param kind Тип задачи (Ingest/History).
    std::uint64_t get_total_processed(JobKind kind) const;

    /// \brief Суммарное время ожидания в очереди (мс, по типу).
    /// \param kind Тип задачи.
    std::uint64_t get_total_wait_ms(JobKind kind) const;

    /// \brief Среднее время ожидания в очереди (мс, по типу).
    /// \param kind Тип задачи.
    /// \return avg_wait_ms = total_wait_ms / total_processed (или 0.0 если processed == 0).
    double get_avg_wait_ms(JobKind kind) const;

private:
    /// \brief Рабочий цикл воркера.
    void worker_loop();

    TaskScheduler& scheduler_; ///< Ссылка на планировщик.
    std::vector<std::thread> workers_; ///< Пул потоков.
    std::atomic<std::uint64_t> total_processed_[2]{0, 0}; ///< [Ingest, History] (C++17: fetch_add).
    std::atomic<std::uint64_t> total_wait_ms_[2]{0, 0}; ///< [Ingest, History].
};

} // namespace dfh_node
