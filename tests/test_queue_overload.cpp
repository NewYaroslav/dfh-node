/// \file test_queue_overload.cpp
/// \brief Тесты переполнения high/low очередей через реальный transport.
/// \details Использует blocking-adapter, чтобы заполнить worker и очередь,
/// проверить reject-path и поведение после освобождения очереди.
///
#include "transport_test_utils.hpp"

#include <future>

namespace {

using test_support::HttpHeaders;
using test_support::make_history_control;
using test_support::make_http_base_config;
using test_support::make_ingest_body;
using test_support::make_ingest_structured_control;
using test_support::make_ws_base_config;
using test_support::RunningHttpNodeWithAdapter;
using test_support::RunningWsNodeWithAdapter;
using test_support::TestWsClient;
using test_support::wait_json_response;

class BlockingAdapter final : public dfh_node::IDfhAdapter {
public:
    void block_ingest(bool value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_block_ingest = value;
    }

    void block_history(bool value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_block_history = value;
    }

    void wait_until_ingest_started(const std::size_t count) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this, count]() { return m_ingest_started >= count; });
    }

    void wait_until_history_started(const std::size_t count) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this, count]() { return m_history_started >= count; });
    }

    void release_ingest(const std::size_t count) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ingest_release_budget += count;
        m_cv.notify_all();
    }

    void release_history(const std::size_t count) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_history_release_budget += count;
        m_cv.notify_all();
    }

    std::unique_ptr<dfh_node::IngestResponse> ingest_structured(std::unique_ptr<dfh_node::IngestRequest>) override {
        wait_if_needed(true);
        auto response = std::make_unique<dfh_node::IngestResponse>();
        response->status = dfh_node::AdapterStatus::Ok;
        return response;
    }

    std::unique_ptr<dfh_node::MergeBlockDfhbinResponse>
    merge_block_dfhbin(std::unique_ptr<dfh_node::MergeBlockDfhbinRequest>) override {
        auto response = std::make_unique<dfh_node::MergeBlockDfhbinResponse>();
        response->status = dfh_node::AdapterStatus::Error;
        response->error_code = "not_supported";
        return response;
    }

    std::unique_ptr<dfh_node::QueryHistoryResponse>
    query_history(std::unique_ptr<dfh_node::QueryHistoryRequest>) override {
        wait_if_needed(false);
        auto response = std::make_unique<dfh_node::QueryHistoryResponse>();
        response->status = dfh_node::AdapterStatus::Ok;
        dfh_node::HistoryChunk chunk;
        chunk.key.provider = "binance";
        chunk.key.symbol = "BTCUSDT";
        chunk.key.source = "spot";
        chunk.key.tf = dfh_node::Timeframe::Ticks;
        chunk.key.block_ts = 1704067200000LL;
        chunk.payload = {1, 2, 3};
        response->chunks.push_back(std::move(chunk));
        return response;
    }

    std::unique_ptr<dfh_node::GetBlockDfhbinResponse>
    get_block_dfhbin(std::unique_ptr<dfh_node::GetBlockDfhbinRequest>) override {
        auto response = std::make_unique<dfh_node::GetBlockDfhbinResponse>();
        response->status = dfh_node::AdapterStatus::Error;
        response->error_code = "not_supported";
        return response;
    }

    std::unique_ptr<dfh_node::ListBlockMetaResponse>
    list_block_meta(std::unique_ptr<dfh_node::ListBlockMetaRequest>) override {
        auto response = std::make_unique<dfh_node::ListBlockMetaResponse>();
        response->status = dfh_node::AdapterStatus::Error;
        response->error_code = "not_supported";
        return response;
    }

    std::unique_ptr<dfh_node::GetBlockHashResponse>
    get_block_hash(std::unique_ptr<dfh_node::GetBlockHashRequest>) override {
        auto response = std::make_unique<dfh_node::GetBlockHashResponse>();
        response->status = dfh_node::AdapterStatus::Error;
        response->error_code = "not_supported";
        return response;
    }

private:
    void wait_if_needed(const bool ingest) {
        std::unique_lock<std::mutex> lock(m_mutex);
        std::size_t &started = ingest ? m_ingest_started : m_history_started;
        bool &blocked = ingest ? m_block_ingest : m_block_history;
        std::size_t &budget = ingest ? m_ingest_release_budget : m_history_release_budget;

        ++started;
        m_cv.notify_all();
        m_cv.wait(lock, [&blocked, &budget]() { return !blocked || budget > 0; });
        if (blocked && budget > 0) {
            --budget;
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_block_ingest{false};
    bool m_block_history{false};
    std::size_t m_ingest_started{0};
    std::size_t m_history_started{0};
    std::size_t m_ingest_release_budget{0};
    std::size_t m_history_release_budget{0};
};

HttpHeaders json_headers() {
    HttpHeaders headers;
    headers.emplace("Content-Type", "application/json");
    return headers;
}

void test_http_high_priority_overload_and_recovery() {
    auto cfg = make_http_base_config();
    cfg.queues.high_capacity = 1;
    cfg.queues.low_capacity = 1;
    cfg.queues.workers = 1;

    BlockingAdapter adapter;
    adapter.block_ingest(true);
    RunningHttpNodeWithAdapter node(std::move(cfg), "token-http-overload", adapter);

    auto first = std::async(std::launch::async, [&]() {
        return node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, json_headers());
    });
    adapter.wait_until_ingest_started(1);

    auto second = std::async(std::launch::async, [&]() {
        return node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, json_headers());
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto rejected = node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, json_headers());
    CHECK_EQ(rejected.status, 503);
    CHECK_EQ(nlohmann::json::parse(rejected.body).at("error").get<std::string>(), "queue_full");

    const auto status = node.request("GET", "/v1/status");
    const auto status_json = nlohmann::json::parse(status.body);
    CHECK(status_json.at("queues").at("high_priority_queue").at("rejected_count").get<std::uint64_t>() >= 1U);

    adapter.release_ingest(2);
    CHECK_EQ(first.get().status, 200);
    CHECK_EQ(second.get().status, 200);

    adapter.block_ingest(false);
    const auto recovered = node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, json_headers());
    CHECK_EQ(recovered.status, 200);
}

void test_http_low_priority_overload() {
    auto cfg = make_http_base_config();
    cfg.queues.high_capacity = 1;
    cfg.queues.low_capacity = 1;
    cfg.queues.workers = 1;

    BlockingAdapter adapter;
    adapter.block_history(true);
    RunningHttpNodeWithAdapter node(std::move(cfg), "token-http-overload-history", adapter);

    const std::string path =
        "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1704067200000&to_ms=1704070800000";

    auto first = std::async(std::launch::async, [&]() { return node.request("GET", path); });
    adapter.wait_until_history_started(1);
    auto second = std::async(std::launch::async, [&]() { return node.request("GET", path); });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto rejected = node.request("GET", path);
    CHECK_EQ(rejected.status, 503);
    CHECK_EQ(nlohmann::json::parse(rejected.body).at("error").get<std::string>(), "queue_full");

    adapter.release_history(2);
    CHECK_EQ(first.get().status, 200);
    CHECK_EQ(second.get().status, 200);
}

void test_ws_overload_error_codes() {
    {
        auto cfg = make_ws_base_config();
        cfg.queues.high_capacity = 0;
        cfg.queues.low_capacity = 1;
        cfg.queues.workers = 0;
        BlockingAdapter adapter;
        RunningWsNodeWithAdapter node(std::move(cfg), "token-ws-overload-high", adapter);
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(make_ingest_structured_control("ov-high").dump());
        CHECK_EQ(wait_json_response(client).at("error_code").get<std::string>(), "overload.high_priority_queue_full");
    }

    {
        auto cfg = make_ws_base_config();
        cfg.queues.high_capacity = 1;
        cfg.queues.low_capacity = 0;
        cfg.queues.workers = 0;
        BlockingAdapter adapter;
        RunningWsNodeWithAdapter node(std::move(cfg), "token-ws-overload-low", adapter);
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(make_history_control("ov-low").dump());
        CHECK_EQ(wait_json_response(client).at("error_code").get<std::string>(), "overload.low_priority_queue_full");
    }
}

} // namespace

int main() {
    test_http_high_priority_overload_and_recovery();
    test_http_low_priority_overload();
    test_ws_overload_error_codes();
    return 0;
}
