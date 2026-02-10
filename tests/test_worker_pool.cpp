/**
 * \file test_worker_pool.cpp
 * \brief Unit-тесты для WorkerPool.
 * \details Проверяет shutdown и сбор метрик обработки.
 */
#include "test_helpers.hpp"
#include "worker_pool.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using namespace dfh_node;

// Тест: shutdown останавливает воркеры без зависаний (stop-now, не drain).
void test_shutdown_stop_now() {
    TaskScheduler scheduler(10, 10);
    WorkerPool pool(2, scheduler);

    pool.start();

    // Enqueue несколько задач.
    for (int i = 0; i < 5; ++i) {
        Task task{TaskKind::Ingest, "req" + std::to_string(i), 0, []() {
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds(10));
                  }};
        scheduler.enqueue_high(std::move(task));
    }

    // Shutdown (stop-now, не дожидаемся пустых очередей).
    auto start = std::chrono::steady_clock::now();
    pool.shutdown();
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Проверка: join завершился за разумное время (<1 сек).
    CHECK(elapsed < std::chrono::seconds(1));
}

// Тест: метрики обработки (per-kind).
void test_metrics_collection() {
    TaskScheduler scheduler(10, 10);
    WorkerPool pool(2, scheduler);

    pool.start();

    // Enqueue 10 ingest jobs (monotonic clock timestamp).
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();

    std::atomic<int> executed{0};
    for (int i = 0; i < 10; ++i) {
        Task task{TaskKind::Ingest, "req" + std::to_string(i),
                  static_cast<std::uint64_t>(now_ms), [&executed]() {
                      executed.fetch_add(1, std::memory_order_relaxed);
                  }};
        auto result = scheduler.enqueue_high(std::move(task));
        CHECK(result.status == EnqueueStatus::Ok);
    }

    // Ждём обработки всех задач (polling на executed).
    while (executed.load(std::memory_order_relaxed) < 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Проверка метрик.
    auto metrics = scheduler.high_metrics();
    CHECK_EQ(metrics.total_enqueued, 10);
    CHECK_EQ(pool.total_processed(TaskLane::High), 10);
    CHECK_EQ(pool.total_processed(TaskLane::Low), 0);
    CHECK(pool.avg_wait_ms(TaskLane::High) >= 0.0);

    pool.shutdown();
}

int main() {
    test_shutdown_stop_now();
    test_metrics_collection();
    return 0;
}
