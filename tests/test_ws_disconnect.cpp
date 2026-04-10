/// \file test_ws_disconnect.cpp
/// \brief Тесты disconnect-сценариев WS transport.
/// \details Проверяет отсутствие crash при разрывах, очистку pending `dfhbin`,
/// возврат счётчика соединений к нулю и timeout на занятой очереди.
///
#include "transport_test_utils.hpp"

namespace {

using test_support::all_scopes_mask;
using test_support::EpochSystemClock;
using test_support::HttpClient;
using test_support::HttpHeaders;
using test_support::HttpResponse;
using test_support::make_api_keys;
using test_support::make_dfhbin_control;
using test_support::make_history_control;
using test_support::make_ingest_structured_control;
using test_support::make_ws_base_config;
using test_support::parse_status_code;
using test_support::RunningWsNodeWithAdapter;
using test_support::TestWsClient;
using test_support::to_size_t;
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

class RunningRuntimeNode {
public:
    RunningRuntimeNode(dfh_node::config::Config cfg, std::string token, dfh_node::IDfhAdapter &adapter)
        : m_cfg(std::move(cfg)), m_token(std::move(token)), m_fingerprint_computer(m_cfg.security.server_secret),
          m_api_key_entries(make_api_keys(m_fingerprint_computer, m_token, all_scopes_mask())),
          m_api_key_store(m_api_key_entries), m_auth_cache(m_cfg.auth.cache_ttl_ms),
          m_auth_service(m_api_key_store, m_auth_cache, m_fingerprint_computer),
          m_rate_limiter(m_cfg.auth.rps_limit, m_cfg.auth.rate_limit_window_ms),
          m_ws_connection_limiter(m_cfg.ws.max_ws_connections_total),
          m_nonce_store(m_cfg.security.anti_replay.enabled ? std::make_unique<dfh_node::NonceStore>(
                                                                 m_epoch_clock, m_cfg.security.anti_replay.nonce_ttl_ms,
                                                                 m_cfg.security.anti_replay.nonce_capacity)
                                                           : nullptr),
          m_anti_replay_validator(m_nonce_store ? std::make_unique<dfh_node::AntiReplayValidator>(
                                                      m_cfg.security.anti_replay, m_epoch_clock, *m_nonce_store)
                                                : nullptr),
          m_scheduler(to_size_t(m_cfg.queues.high_capacity), to_size_t(m_cfg.queues.low_capacity)),
          m_worker_pool(static_cast<std::size_t>(std::max(0, m_cfg.queues.workers)), m_scheduler),
          m_storage_root(std::filesystem::temp_directory_path() /
                         ("dfh-node-runtime-disconnect-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
          m_mdbx_path(m_storage_root / "keys.mdbx"),
          m_mdbx_store(std::make_unique<dfh_node::MdbxApiKeyStore>(m_mdbx_path.string())),
          m_disk_monitor(m_storage_root.string(), static_cast<std::uint64_t>(m_cfg.storage.min_free_bytes)),
          m_adapter(adapter), m_registry(std::make_shared<dfh_node::transport::WsSessionRegistry>()),
          m_gate(m_auth_service, m_rate_limiter, m_ws_connection_limiter, m_anti_replay_validator.get(),
                 m_cfg.security.anti_replay.require_for_scopes),
          m_http_router(m_gate, m_scheduler, m_adapter, m_cfg, &m_disk_monitor, m_mdbx_store.get()),
          m_http_server(m_cfg.http, m_http_router),
          m_ws_router(m_gate, m_scheduler, m_adapter, m_cfg, m_registry, &m_disk_monitor),
          m_ws_server(m_cfg.ws, m_ws_router) {
        std::filesystem::create_directories(m_storage_root);
        m_mdbx_store->open();
        if (m_cfg.queues.workers > 0) {
            m_worker_pool.start();
        }
        m_http_server.start();
        m_ws_server.start();
        wait_until_ready();
    }

    ~RunningRuntimeNode() {
        m_ws_server.shutdown();
        m_http_server.shutdown();
        m_worker_pool.shutdown();
        m_scheduler.shutdown();
    }

    std::string endpoint(const std::string &path) const { return "127.0.0.1:" + std::to_string(m_cfg.ws.port) + path; }
    const std::string &token() const { return m_token; }

    HttpResponse request(const std::string &method, const std::string &path, const std::string &body = "",
                         const bool with_token = true, const long timeout_seconds = 5,
                         const HttpHeaders &extra_headers = HttpHeaders()) const {
        HttpClient client("127.0.0.1:" + std::to_string(m_cfg.http.port));
        client.config.timeout = timeout_seconds;
        HttpHeaders headers = extra_headers;
        if (with_token) {
            headers.emplace("Authorization", "Bearer " + m_token);
        }
        auto response = client.request(method, path, body, headers);
        HttpResponse result;
        result.status = parse_status_code(response->status_code);
        result.body = response->content.string();
        result.headers = response->header;
        return result;
    }

private:
    void wait_until_ready() const {
        for (int attempt = 0; attempt < 120; ++attempt) {
            try {
                const auto response = request("GET", "/v1/status");
                if (response.status == 200) {
                    return;
                }
            } catch (...) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        CHECK(false);
    }

    dfh_node::config::Config m_cfg;
    std::string m_token;
    dfh_node::FingerprintComputer m_fingerprint_computer;
    std::vector<dfh_node::config::ApiKeyEntry> m_api_key_entries;
    dfh_node::ConfigApiKeyStore m_api_key_store;
    dfh_node::AuthCache m_auth_cache;
    dfh_node::AuthService m_auth_service;
    dfh_node::RateLimiter m_rate_limiter;
    dfh_node::WsConnectionLimiter m_ws_connection_limiter;
    EpochSystemClock m_epoch_clock;
    std::unique_ptr<dfh_node::NonceStore> m_nonce_store;
    std::unique_ptr<dfh_node::AntiReplayValidator> m_anti_replay_validator;
    dfh_node::TaskScheduler m_scheduler;
    dfh_node::WorkerPool m_worker_pool;
    std::filesystem::path m_storage_root;
    std::filesystem::path m_mdbx_path;
    std::unique_ptr<dfh_node::MdbxApiKeyStore> m_mdbx_store;
    dfh_node::DiskMonitor m_disk_monitor;
    dfh_node::IDfhAdapter &m_adapter;
    std::shared_ptr<dfh_node::transport::WsSessionRegistry> m_registry;
    dfh_node::UnifiedGate m_gate;
    dfh_node::transport::HttpRouter m_http_router;
    dfh_node::transport::HttpServer m_http_server;
    dfh_node::transport::WsRouter m_ws_router;
    dfh_node::transport::WsServer m_ws_server;
};

void test_ingest_disconnect_does_not_crash() {
    auto cfg = make_ws_base_config();
    cfg.queues.workers = 1;
    BlockingAdapter adapter;
    adapter.block_ingest(true);
    RunningRuntimeNode node(std::move(cfg), "token-ws-disconnect-ingest", adapter);

    {
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(make_ingest_structured_control("close-ingest").dump());
        adapter.wait_until_ingest_started(1);
        client.stop();
    }

    adapter.release_ingest(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    TestWsClient probe(node.endpoint("/ws/json"), node.token());
    CHECK(probe.wait_open());
    probe.send_text(make_history_control("probe-after-ingest-close").dump());
    CHECK_EQ(wait_json_response(probe).at("ok").get<bool>(), true);
}

void test_pending_dfhbin_is_cleared_after_disconnect() {
    auto cfg = make_ws_base_config();
    BlockingAdapter adapter;
    RunningRuntimeNode node(std::move(cfg), "token-ws-disconnect-dfhbin", adapter);

    {
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(make_dfhbin_control("pending-before-close", std::string(64, 'a')).dump());
        client.stop();
    }

    TestWsClient next(node.endpoint("/ws/json"), node.token());
    CHECK(next.wait_open());

    const std::vector<std::uint8_t> payload = {1, 2, 3, 4};
    next.send_binary(payload);
    CHECK_EQ(wait_json_response(next).at("error_code").get<std::string>(), "unexpected_binary_frame");

    const std::string raw(reinterpret_cast<const char *>(payload.data()), payload.size());
    next.send_text(make_dfhbin_control("pending-after-reconnect", dfh_node::compute_sha256_hex(raw)).dump());
    next.send_binary(payload);
    CHECK_EQ(wait_json_response(next).at("msg_id").get<std::string>(), "pending-after-reconnect");
}

void test_history_disconnect_does_not_crash() {
    auto cfg = make_ws_base_config();
    cfg.queues.workers = 1;
    BlockingAdapter adapter;
    adapter.block_history(true);
    RunningRuntimeNode node(std::move(cfg), "token-ws-disconnect-history", adapter);

    {
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(make_history_control("close-history").dump());
        adapter.wait_until_history_started(1);
        client.stop();
    }

    adapter.release_history(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    adapter.block_history(false);

    TestWsClient probe(node.endpoint("/ws/json"), node.token());
    CHECK(probe.wait_open());
    probe.send_text(make_history_control("probe-after-history-close").dump());
    CHECK_EQ(wait_json_response(probe).at("ok").get<bool>(), true);
}

void test_status_reports_zero_active_connections_after_disconnects() {
    auto cfg = make_ws_base_config();
    BlockingAdapter adapter;
    RunningRuntimeNode node(std::move(cfg), "token-ws-status", adapter);

    for (int i = 0; i < 5; ++i) {
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.close_graceful();
        (void)client.wait_close(std::chrono::milliseconds(1000));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    const auto status = node.request("GET", "/v1/status");
    const auto status_json = nlohmann::json::parse(status.body);
    CHECK_EQ(status_json.at("ws_active_connections_total").get<std::int64_t>(), 0);
}

void test_timeout_when_queue_is_busy() {
    auto cfg = make_ws_base_config();
    cfg.ws.request_timeout_ms = 50;
    cfg.queues.workers = 1;
    BlockingAdapter adapter;
    adapter.block_history(true);
    RunningRuntimeNode node(std::move(cfg), "token-ws-timeout", adapter);

    TestWsClient client(node.endpoint("/ws/json"), node.token());
    CHECK(client.wait_open());

    client.send_text(make_history_control("busy-1").dump());
    adapter.wait_until_history_started(1);
    client.send_text(make_history_control("busy-2").dump());
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    adapter.release_history(1);

    const auto first = wait_json_response(client);
    const auto second = wait_json_response(client);
    CHECK_EQ(first.at("msg_id").get<std::string>(), "busy-1");
    CHECK_EQ(first.at("ok").get<bool>(), true);
    CHECK_EQ(second.at("msg_id").get<std::string>(), "busy-2");
    CHECK_EQ(second.at("error_code").get<std::string>(), "timeout");
}

} // namespace

int main() {
    test_ingest_disconnect_does_not_crash();
    test_pending_dfhbin_is_cleared_after_disconnect();
    test_history_disconnect_does_not_crash();
    test_status_reports_zero_active_connections_after_disconnects();
    test_timeout_when_queue_is_busy();
    return 0;
}
