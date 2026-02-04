/**
 * \file worker_pool.cpp
 * \brief Реализация пула воркеров для выполнения задач.
 * \details Использует TaskScheduler для получения задач и собирает метрики ожидания.
 */
#include "dfh_node/worker_pool.hpp"

#include <chrono>

namespace dfh_node {

WorkerPool::WorkerPool(std::size_t num_workers, TaskScheduler& scheduler)
    : scheduler_(scheduler) {
    workers_.reserve(num_workers);
}

WorkerPool::~WorkerPool() {
    shutdown();
}

void WorkerPool::start() {
    for (std::size_t i = 0; i < workers_.capacity(); ++i) {
        workers_.emplace_back(&WorkerPool::worker_loop, this);
    }
}

void WorkerPool::shutdown() {
    if (workers_.empty()) {
        return; // Уже остановлен или не запускался.
    }

    scheduler_.shutdown(); // Остановить планировщик (пробуждает воркеры).

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join(); // Graceful: ждём завершения без зависаний.
        }
    }

    workers_.clear();
}

void WorkerPool::worker_loop() {
    while (true) {
        auto job_opt = scheduler_.pop_next_job(); // Blocking, без timeout.
        if (!job_opt) {
            break; // Shutdown.
        }

        // КРИТИЧНО: monotonic clock (steady_clock, НЕ system_clock).
        const auto now = std::chrono::steady_clock::now();
        const std::uint64_t now_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        const std::uint64_t wait_ms = now_ms - job_opt->enqueue_ts_ms;

        const int kind_idx = to_index(job_opt->kind);

        // C++17: явный fetch_add вместо operator+= (избегаем неоднозначной семантики).
        total_wait_ms_[kind_idx].fetch_add(wait_ms, std::memory_order_relaxed);

        // Выполнить задачу.
        job_opt->payload();

        total_processed_[kind_idx].fetch_add(1, std::memory_order_relaxed);
    }
}

std::uint64_t WorkerPool::get_total_processed(JobKind kind) const {
    const int idx = to_index(kind);
    return total_processed_[idx].load(std::memory_order_relaxed);
}

std::uint64_t WorkerPool::get_total_wait_ms(JobKind kind) const {
    const int idx = to_index(kind);
    return total_wait_ms_[idx].load(std::memory_order_relaxed);
}

double WorkerPool::get_avg_wait_ms(JobKind kind) const {
    const int idx = to_index(kind);
    const auto processed = total_processed_[idx].load(std::memory_order_relaxed);
    if (processed == 0) {
        return 0.0;
    }
    const auto wait_ms = total_wait_ms_[idx].load(std::memory_order_relaxed);
    return static_cast<double>(wait_ms) / static_cast<double>(processed);
}

} // namespace dfh_node
