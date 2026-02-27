/// \file test_task_scheduler.cpp
/// \brief Юнит-тесты для TaskScheduler.
/// \details Проверяет вместимость очередей, приоритет и корректное пробуждение при остановке.
///
#include "scheduler.hpp"
#include "test_helpers.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace dfh_node;

// Тест: заполнение очереди до предела и проверка отклонения.
void test_capacity_reject() {
    TaskScheduler scheduler(2, 2); // high_cap=2, low_cap=2

    // Заполняем high-очередь до предела.
    Task task1{TaskKind::Ingest, "req1", 0, []() {}};
    Task task2{TaskKind::Ingest, "req2", 0, []() {}};
    auto r1 = scheduler.enqueue_high(std::move(task1));
    auto r2 = scheduler.enqueue_high(std::move(task2));
    CHECK(r1.status == EnqueueStatus::Ok);
    CHECK(r2.status == EnqueueStatus::Ok);

    // Попытка добавить третью задачу -> отклонение.
    Task task3{TaskKind::Ingest, "req3", 0, []() {}};
    auto r3 = scheduler.enqueue_high(std::move(task3));
    CHECK(r3.status == EnqueueStatus::Rejected);
    CHECK_EQ(r3.error_code, "overload.high_priority_queue_full");

    // Проверяем rejected_count.
    auto metrics = scheduler.high_metrics();
    CHECK_EQ(metrics.rejected_count, 1);
    CHECK_EQ(metrics.total_enqueued, 2);

    scheduler.shutdown();
}

// Тест: приоритет high > low при одном рабочем потоке.
void test_priority_single_worker() {
    TaskScheduler scheduler(10, 10);

    // Добавляем 5 задач в high и 5 задач в low.
    for (int i = 0; i < 5; ++i) {
        Task task{TaskKind::Ingest, "ingest" + std::to_string(i), 0, []() {}};
        scheduler.enqueue_high(std::move(task));
    }
    for (int i = 0; i < 5; ++i) {
        Task task{TaskKind::History, "history" + std::to_string(i), 0, []() {}};
        scheduler.enqueue_low(std::move(task));
    }

    // Один воркер извлекает задачи по порядку.
    std::vector<TaskKind> order;
    for (int i = 0; i < 10; ++i) {
        auto task_opt = scheduler.pop_next_task();
        CHECK(task_opt.has_value());
        order.push_back(task_opt->kind);
    }

    // Проверка: первые 5 — Ingest, следующие 5 — History.
    for (int i = 0; i < 5; ++i) {
        CHECK(order[i] == TaskKind::Ingest);
    }
    for (int i = 5; i < 10; ++i) {
        CHECK(order[i] == TaskKind::History);
    }

    scheduler.shutdown();
}

// Тест: остановка пробуждает ожидающий поток.
void test_shutdown_unblocks() {
    TaskScheduler scheduler(10, 10);

    std::atomic<bool> finished{false};
    std::thread worker([&scheduler, &finished]() {
        auto task_opt = scheduler.pop_next_task();
        CHECK(!task_opt.has_value());
        finished.store(true, std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler.shutdown();

    worker.join();
    CHECK(finished.load(std::memory_order_relaxed));
}

int main() {
    test_capacity_reject();
    test_priority_single_worker();
    test_shutdown_unblocks();
    return 0;
}
