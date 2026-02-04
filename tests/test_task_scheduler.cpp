/**
 * \file test_task_scheduler.cpp
 * \brief Unit-тесты для TaskScheduler.
 * \details Проверяет capacity, приоритет и корректное пробуждение при shutdown.
 */
#include "dfh_node/task_scheduler.hpp"
#include "test_helpers.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace dfh_node;

// Тест: заполнение очереди до capacity, проверка reject.
void test_capacity_reject() {
    TaskScheduler scheduler(2, 2); // ingest_cap=2, history_cap=2

    // Заполняем ingest до capacity.
    Job job1{JobKind::Ingest, "req1", 0, []() {}};
    Job job2{JobKind::Ingest, "req2", 0, []() {}};
    auto r1 = scheduler.enqueue_ingest(std::move(job1));
    auto r2 = scheduler.enqueue_ingest(std::move(job2));
    CHECK(r1.status == EnqueueStatus::Ok);
    CHECK(r2.status == EnqueueStatus::Ok);

    // Попытка добавить третью задачу -> reject.
    Job job3{JobKind::Ingest, "req3", 0, []() {}};
    auto r3 = scheduler.enqueue_ingest(std::move(job3));
    CHECK(r3.status == EnqueueStatus::Rejected);
    CHECK_EQ(r3.error_code, "overload.ingest_queue_full");

    // Проверяем rejected_count.
    auto metrics = scheduler.get_ingest_metrics();
    CHECK_EQ(metrics.rejected_count, 1);
    CHECK_EQ(metrics.total_enqueued, 2);

    scheduler.shutdown();
}

// Тест: приоритет ingest > history при одном воркере.
void test_priority_single_worker() {
    TaskScheduler scheduler(10, 10);

    // Enqueue 5 ingest + 5 history.
    for (int i = 0; i < 5; ++i) {
        Job job{JobKind::Ingest, "ingest" + std::to_string(i), 0, []() {}};
        scheduler.enqueue_ingest(std::move(job));
    }
    for (int i = 0; i < 5; ++i) {
        Job job{JobKind::History, "history" + std::to_string(i), 0, []() {}};
        scheduler.enqueue_history(std::move(job));
    }

    // Один воркер извлекает задачи по порядку.
    std::vector<JobKind> order;
    for (int i = 0; i < 10; ++i) {
        auto job_opt = scheduler.pop_next_job();
        CHECK(job_opt.has_value());
        order.push_back(job_opt->kind);
    }

    // Проверка: первые 5 — Ingest, следующие 5 — History.
    for (int i = 0; i < 5; ++i) {
        CHECK(order[i] == JobKind::Ingest);
    }
    for (int i = 5; i < 10; ++i) {
        CHECK(order[i] == JobKind::History);
    }

    scheduler.shutdown();
}

// Тест: shutdown пробуждает ожидающий поток.
void test_shutdown_unblocks() {
    TaskScheduler scheduler(10, 10);

    std::atomic<bool> finished{false};
    std::thread worker([&scheduler, &finished]() {
        auto job_opt = scheduler.pop_next_job();
        CHECK(!job_opt.has_value());
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
