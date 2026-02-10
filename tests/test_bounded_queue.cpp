/// \file test_bounded_queue.cpp
/// \brief Unit-тесты для внутренней bounded-очереди.
/// \details Проверяет capacity, FIFO-порядок и счётчики отклонений/enqueue.
///
#include "internal/bounded_queue.hpp"
#include "test_helpers.hpp"

#include <string>

using namespace dfh_node;

namespace {

Task make_task(const TaskKind kind, const std::string &request_id) {
    Task task{};
    task.kind = kind;
    task.request_id = request_id;
    task.enqueue_ts_ms = 0;
    task.payload = []() {};
    task.lane = (kind == TaskKind::Ingest) ? TaskLane::High : TaskLane::Low;
    return task;
}

void test_capacity_and_counters() {
    BoundedQueue queue(2);

    CHECK(queue.try_push(make_task(TaskKind::Ingest, "req-1")));
    CHECK(queue.try_push(make_task(TaskKind::History, "req-2")));
    CHECK(!queue.try_push(make_task(TaskKind::Ingest, "req-3")));

    CHECK_EQ(queue.capacity(), 2);
    CHECK_EQ(queue.size(), 2);
    CHECK(queue.full());
    CHECK_EQ(queue.rejected_count(), 1);
    CHECK_EQ(queue.total_enqueued(), 2);
}

void test_fifo_order_and_empty_state() {
    BoundedQueue queue(3);

    CHECK(queue.empty());
    CHECK(!queue.try_pop().has_value());

    CHECK(queue.try_push(make_task(TaskKind::Ingest, "first")));
    CHECK(queue.try_push(make_task(TaskKind::History, "second")));

    auto first = queue.try_pop();
    CHECK(first.has_value());
    CHECK_EQ(first->request_id, "first");
    CHECK_EQ(first->kind, TaskKind::Ingest);

    auto second = queue.try_pop();
    CHECK(second.has_value());
    CHECK_EQ(second->request_id, "second");
    CHECK_EQ(second->kind, TaskKind::History);

    CHECK(queue.empty());
    CHECK(!queue.try_pop().has_value());
}

} // namespace

int main() {
    test_capacity_and_counters();
    test_fifo_order_and_empty_state();
    return 0;
}
