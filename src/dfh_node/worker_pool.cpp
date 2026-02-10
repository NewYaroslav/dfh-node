/**
 * \file worker_pool.cpp
 * \brief Реализация пула воркеров для выполнения задач.
 * \details Использует TaskScheduler для получения задач и собирает метрики
 * ожидания.
 */
#include "worker_pool.hpp"

#include <chrono>

namespace dfh_node {

WorkerPool::WorkerPool(std::size_t num_workers, TaskScheduler &scheduler)
    : m_scheduler(scheduler) {
    m_workers.reserve(num_workers);
}

WorkerPool::~WorkerPool() { shutdown(); }

void WorkerPool::start() {
    for (std::size_t i = 0; i < m_workers.capacity(); ++i) {
        m_workers.emplace_back(&WorkerPool::worker_loop, this);
    }
}

void WorkerPool::shutdown() {
    if (m_workers.empty()) {
        return; // Уже остановлен или не запускался.
    }

    m_scheduler.shutdown(); // Остановить планировщик (пробуждает воркеры).

    for (auto &worker : m_workers) {
        if (worker.joinable()) {
            worker.join(); // Graceful: ждём завершения без зависаний.
        }
    }

    m_workers.clear();
}

void WorkerPool::worker_loop() {
    while (true) {
        auto task_opt = m_scheduler.pop_next_task(); // Blocking, без timeout.
        if (!task_opt) {
            break; // Shutdown.
        }

        // КРИТИЧНО: monotonic clock (steady_clock, НЕ system_clock).
        const auto now = std::chrono::steady_clock::now();
        const std::uint64_t now_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count());
        const std::uint64_t wait_ms = now_ms - task_opt->enqueue_ts_ms;

        const int lane_idx = to_index(task_opt->lane);

        // C++17: явный fetch_add вместо operator+= (избегаем неоднозначной
        // семантики).
        m_total_wait_ms[lane_idx].fetch_add(wait_ms, std::memory_order_relaxed);

        // Выполнить задачу.
        task_opt->payload();

        m_total_processed[lane_idx].fetch_add(1, std::memory_order_relaxed);
    }
}

std::uint64_t WorkerPool::total_processed(TaskLane lane) const {
    const int idx = to_index(lane);
    return m_total_processed[idx].load(std::memory_order_relaxed);
}

std::uint64_t WorkerPool::total_wait_ms(TaskLane lane) const {
    const int idx = to_index(lane);
    return m_total_wait_ms[idx].load(std::memory_order_relaxed);
}

double WorkerPool::avg_wait_ms(TaskLane lane) const {
    const int idx = to_index(lane);
    const auto processed =
        m_total_processed[idx].load(std::memory_order_relaxed);
    if (processed == 0) {
        return 0.0;
    }
    const auto wait_ms = m_total_wait_ms[idx].load(std::memory_order_relaxed);
    return static_cast<double>(wait_ms) / static_cast<double>(processed);
}

} // namespace dfh_node
