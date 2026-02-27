/// \file test_dfh_adapter_e2e.cpp
/// \brief E2E-тест контракта `IDfhAdapter` через `TaskScheduler` и `WorkerPool`.
/// \details Проверяет синхронные вызовы `FakeDfhAdapter` из `Task::payload()` и
/// получение результатов через `std::promise`/`std::future`.
///
#include "adapter.hpp"
#include "scheduler.hpp"
#include "test_helpers.hpp"

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <utility>
#include <vector>

using namespace dfh_node;

namespace {

std::uint64_t steady_now_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

BlockKey make_key(std::int64_t block_ts) {
    BlockKey key;
    key.provider = "p1";
    key.symbol = "s1";
    key.source = "src";
    key.tf = Timeframe::Ticks;
    key.block_ts = block_ts;
    return key;
}

void test_dfh_adapter_e2e_via_worker_pool() {
    constexpr std::int64_t hour_ms = 3600LL * 1000;
    TaskScheduler scheduler(10, 10);
    WorkerPool pool(2, scheduler);
    FakeDfhAdapter adapter;

    pool.start();

    auto ingest_req_holder = std::make_shared<std::unique_ptr<IngestRequest>>(std::make_unique<IngestRequest>());
    (*ingest_req_holder)->key = make_key(hour_ms);
    (*ingest_req_holder)->payload = std::vector<std::uint8_t>{1, 2, 3, 4};
    auto ingest_promise = std::make_shared<std::promise<AdapterStatus>>();
    auto ingest_future = ingest_promise->get_future();

    Task ingest_task;
    ingest_task.kind = TaskKind::Ingest;
    ingest_task.request_id = "e2e_ingest";
    ingest_task.enqueue_ts_ms = steady_now_ms();
    ingest_task.lane = TaskLane::High;
    ingest_task.payload = [dto_holder = ingest_req_holder, promise = ingest_promise, &adapter]() mutable {
        auto resp = adapter.ingest_structured(std::move(*dto_holder));
        promise->set_value(resp->status);
    };

    auto ingest_enqueue = scheduler.enqueue_high(std::move(ingest_task));
    CHECK_EQ(ingest_enqueue.status, EnqueueStatus::Ok);
    CHECK_EQ(ingest_future.get(), AdapterStatus::Ok);

    auto history_req_holder =
        std::make_shared<std::unique_ptr<QueryHistoryRequest>>(std::make_unique<QueryHistoryRequest>());
    (*history_req_holder)->provider = "p1";
    (*history_req_holder)->symbol = "s1";
    (*history_req_holder)->source = "src";
    (*history_req_holder)->tf = Timeframe::Ticks;
    (*history_req_holder)->from_ms = hour_ms;
    (*history_req_holder)->to_ms = 2 * hour_ms;
    auto history_promise = std::make_shared<std::promise<std::unique_ptr<QueryHistoryResponse>>>();
    auto history_future = history_promise->get_future();

    Task history_task;
    history_task.kind = TaskKind::History;
    history_task.request_id = "e2e_history";
    history_task.enqueue_ts_ms = steady_now_ms();
    history_task.lane = TaskLane::High;
    history_task.payload = [dto_holder = history_req_holder, promise = history_promise, &adapter]() mutable {
        promise->set_value(adapter.query_history(std::move(*dto_holder)));
    };

    auto history_enqueue = scheduler.enqueue_high(std::move(history_task));
    CHECK_EQ(history_enqueue.status, EnqueueStatus::Ok);
    auto history_resp = history_future.get();
    CHECK(history_resp != nullptr);
    CHECK_EQ(history_resp->status, AdapterStatus::Ok);
    CHECK(!history_resp->chunks.empty());
    CHECK_EQ(history_resp->chunks[0].payload, (std::vector<std::uint8_t>{1, 2, 3, 4}));

    auto block_req_holder =
        std::make_shared<std::unique_ptr<GetBlockDfhbinRequest>>(std::make_unique<GetBlockDfhbinRequest>());
    (*block_req_holder)->key = make_key(hour_ms);
    auto block_promise = std::make_shared<std::promise<std::unique_ptr<GetBlockDfhbinResponse>>>();
    auto block_future = block_promise->get_future();

    Task block_task;
    block_task.kind = TaskKind::History;
    block_task.request_id = "e2e_get_block";
    block_task.enqueue_ts_ms = steady_now_ms();
    block_task.lane = TaskLane::High;
    block_task.payload = [dto_holder = block_req_holder, promise = block_promise, &adapter]() mutable {
        promise->set_value(adapter.get_block_dfhbin(std::move(*dto_holder)));
    };

    auto block_enqueue = scheduler.enqueue_high(std::move(block_task));
    CHECK_EQ(block_enqueue.status, EnqueueStatus::Ok);
    auto block_resp = block_future.get();
    CHECK(block_resp != nullptr);
    CHECK_EQ(block_resp->status, AdapterStatus::Ok);
    CHECK_EQ(block_resp->payload, (std::vector<std::uint8_t>{1, 2, 3, 4}));

    pool.shutdown();
    CHECK(pool.total_processed(TaskLane::High) > 0);
}

} // namespace

int main() {
    test_dfh_adapter_e2e_via_worker_pool();
    return 0;
}
