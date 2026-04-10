/// \file test_http_integration.cpp
/// \brief Интеграционные тесты HTTP transport поверх `HttpServer`/`HttpRouter`.
/// \details Проверяет сценарии успеха, unauthorized, queue overflow, timeout и параллельные запросы.
///
#include "adapter.hpp"
#include "auth.hpp"
#include "config.hpp"
#include "scheduler.hpp"
#include "test_helpers.hpp"
#include "transport.hpp"

#include <client_http.hpp>
#include <nlohmann/json.hpp>

#ifdef USE_STANDALONE_ASIO
#include <asio.hpp>
#include <asio/ip/tcp.hpp>
#else
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
namespace asio = boost::asio;
#endif

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using SwsClient = SimpleWeb::Client<SimpleWeb::HTTP>;
using SwsHeaders = SimpleWeb::CaseInsensitiveMultimap;

struct HttpResponse {
    int status{0};
    std::string body;
    SwsHeaders headers;
};

int parse_status_code(const std::string &value) {
    CHECK(value.size() >= 3);
    return std::stoi(value.substr(0, 3));
}

unsigned short acquire_free_port() {
    asio::io_service io_service;
    asio::ip::tcp::acceptor acceptor(io_service);
    acceptor.open(asio::ip::tcp::v4());
    acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    const unsigned short port = acceptor.local_endpoint().port();
    acceptor.close();
    return port;
}

std::size_t to_size_t(const std::int64_t value) {
    if (value <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(value);
}

bool header_contains(const SwsHeaders &headers, const std::string &key, const std::string &fragment) {
    const auto it = headers.find(key);
    if (it == headers.end()) {
        return false;
    }
    return it->second.find(fragment) != std::string::npos;
}

std::vector<dfh_node::config::ApiKeyEntry> make_api_keys(const dfh_node::FingerprintComputer &computer,
                                                         const std::string &token) {
    std::vector<dfh_node::config::ApiKeyEntry> entries;
    entries.push_back(dfh_node::config::ApiKeyEntry{
        computer.compute(token),
        dfh_node::Scope::Read | dfh_node::Scope::Write,
        std::nullopt,
        100000,
        100,
    });
    return entries;
}

dfh_node::config::Config make_base_config() {
    auto cfg = dfh_node::config::default_config();
    cfg.node_id = "node-http-it";
    cfg.env = "dev";
    cfg.security.server_secret = "0123456789abcdef0123456789abcdef";
    cfg.security.anti_replay.enabled = false;
    cfg.security.anti_replay.require_for_scopes = 0;
    cfg.auth.rps_limit = 100000;
    cfg.auth.rate_limit_window_ms = 1000;
    cfg.auth.cache_ttl_ms = 60000;
    cfg.http.bind_host = "127.0.0.1";
    cfg.http.port = static_cast<int>(acquire_free_port());
    cfg.http.request_timeout_ms = 500;
    cfg.http.max_payload_bytes = 1024 * 1024;
    cfg.http.history_max_range_ms = 24LL * 60LL * 60LL * 1000LL;
    cfg.http.history_max_bytes = 1024 * 1024;
    cfg.storage.min_free_bytes = 0;
    cfg.queues.high_capacity = 64;
    cfg.queues.low_capacity = 64;
    cfg.queues.workers = 2;
    return cfg;
}

class RunningHttpNode {
public:
    RunningHttpNode(dfh_node::config::Config cfg, std::string token, const bool auto_start_workers = true)
        : m_cfg(std::move(cfg)), m_token(std::move(token)), m_fingerprint_computer(m_cfg.security.server_secret),
          m_api_key_entries(make_api_keys(m_fingerprint_computer, m_token)), m_api_key_store(m_api_key_entries),
          m_auth_cache(m_cfg.auth.cache_ttl_ms), m_auth_service(m_api_key_store, m_auth_cache, m_fingerprint_computer),
          m_rate_limiter(m_cfg.auth.rps_limit, m_cfg.auth.rate_limit_window_ms),
          m_gate(m_auth_service, m_rate_limiter, m_ws_connection_limiter, nullptr,
                 m_cfg.security.anti_replay.require_for_scopes),
          m_scheduler(to_size_t(m_cfg.queues.high_capacity), to_size_t(m_cfg.queues.low_capacity)),
          m_worker_pool(static_cast<std::size_t>(m_cfg.queues.workers), m_scheduler),
          m_storage_root(
              std::filesystem::temp_directory_path() /
              ("dfh-node-http-it-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
          m_mdbx_path(m_storage_root / "keys.mdbx"),
          m_mdbx_store(std::make_unique<dfh_node::MdbxApiKeyStore>(m_mdbx_path.string())),
          m_disk_monitor(m_storage_root.string(), static_cast<std::uint64_t>(m_cfg.storage.min_free_bytes)),
          m_adapter(), m_router(m_gate, m_scheduler, m_adapter, m_cfg, &m_disk_monitor, m_mdbx_store.get()),
          m_server(m_cfg.http, m_router) {
        std::filesystem::create_directories(m_storage_root);
        m_mdbx_store->open();
        if (auto_start_workers) {
            start_workers();
        }

        m_server.start();
        wait_until_ready();
    }

    ~RunningHttpNode() {
        m_server.shutdown();
        m_worker_pool.shutdown();
        m_scheduler.shutdown();
    }

    HttpResponse request(const std::string &method, const std::string &path, const std::string &body = "",
                         bool with_token = true, long timeout_seconds = 5,
                         const SwsHeaders &extra_headers = SwsHeaders()) const {
        SwsClient client("127.0.0.1:" + std::to_string(m_cfg.http.port));
        client.config.timeout = timeout_seconds;

        SwsHeaders headers = extra_headers;
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

    void start_workers() {
        if (m_workers_started || m_cfg.queues.workers <= 0) {
            return;
        }
        m_worker_pool.start();
        m_workers_started = true;
    }

    void start_server_again() { m_server.start(); }

    void shutdown_server_for_test() { m_server.shutdown(); }

    dfh_node::transport::HttpExecutor executor() const { return m_server.get_executor(); }
    int port() const { return m_cfg.http.port; }
    const std::string &token() const { return m_token; }

    dfh_node::MdbxApiKeyStore &mdbx_store() { return *m_mdbx_store; }

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
    dfh_node::UnifiedGate m_gate;
    dfh_node::TaskScheduler m_scheduler;
    dfh_node::WorkerPool m_worker_pool;
    std::filesystem::path m_storage_root;
    std::filesystem::path m_mdbx_path;
    std::unique_ptr<dfh_node::MdbxApiKeyStore> m_mdbx_store;
    dfh_node::DiskMonitor m_disk_monitor;
    dfh_node::FakeDfhAdapter m_adapter;
    dfh_node::transport::HttpRouter m_router;
    dfh_node::transport::HttpServer m_server;
    bool m_workers_started{false};
};

class ScriptedHttpAdapter final : public dfh_node::IDfhAdapter {
public:
    enum class Mode {
        Normal,
        HistoryNull,
        HistoryThrow,
        HistoryThrowUnknown,
    };

    void set_mode(const Mode mode) { m_mode = mode; }

    std::unique_ptr<dfh_node::IngestResponse> ingest_structured(std::unique_ptr<dfh_node::IngestRequest>) override {
        auto resp = std::make_unique<dfh_node::IngestResponse>();
        resp->status = dfh_node::AdapterStatus::Ok;
        return resp;
    }

    std::unique_ptr<dfh_node::MergeBlockDfhbinResponse>
    merge_block_dfhbin(std::unique_ptr<dfh_node::MergeBlockDfhbinRequest>) override {
        auto resp = std::make_unique<dfh_node::MergeBlockDfhbinResponse>();
        resp->status = dfh_node::AdapterStatus::Error;
        resp->error_code = "not_supported";
        return resp;
    }

    std::unique_ptr<dfh_node::QueryHistoryResponse>
    query_history(std::unique_ptr<dfh_node::QueryHistoryRequest>) override {
        switch (m_mode) {
        case Mode::HistoryNull:
            return nullptr;
        case Mode::HistoryThrow:
            throw std::runtime_error("history throw");
        case Mode::HistoryThrowUnknown:
            throw 13;
        default: {
            auto resp = std::make_unique<dfh_node::QueryHistoryResponse>();
            resp->status = dfh_node::AdapterStatus::Ok;
            return resp;
        }
        }
    }

    std::unique_ptr<dfh_node::GetBlockDfhbinResponse>
    get_block_dfhbin(std::unique_ptr<dfh_node::GetBlockDfhbinRequest>) override {
        auto resp = std::make_unique<dfh_node::GetBlockDfhbinResponse>();
        resp->status = dfh_node::AdapterStatus::Error;
        resp->error_code = "not_supported";
        return resp;
    }

    std::unique_ptr<dfh_node::ListBlockMetaResponse>
    list_block_meta(std::unique_ptr<dfh_node::ListBlockMetaRequest>) override {
        auto resp = std::make_unique<dfh_node::ListBlockMetaResponse>();
        resp->status = dfh_node::AdapterStatus::Error;
        resp->error_code = "not_supported";
        return resp;
    }

    std::unique_ptr<dfh_node::GetBlockHashResponse>
    get_block_hash(std::unique_ptr<dfh_node::GetBlockHashRequest>) override {
        auto resp = std::make_unique<dfh_node::GetBlockHashResponse>();
        resp->status = dfh_node::AdapterStatus::Error;
        resp->error_code = "not_supported";
        return resp;
    }

private:
    Mode m_mode{Mode::Normal};
};

class RunningHttpNodeWithAdapter {
public:
    RunningHttpNodeWithAdapter(dfh_node::config::Config cfg, std::string token, dfh_node::IDfhAdapter &adapter,
                               const bool auto_start_workers = true)
        : m_cfg(std::move(cfg)), m_token(std::move(token)), m_fingerprint_computer(m_cfg.security.server_secret),
          m_api_key_entries(make_api_keys(m_fingerprint_computer, m_token)), m_api_key_store(m_api_key_entries),
          m_auth_cache(m_cfg.auth.cache_ttl_ms), m_auth_service(m_api_key_store, m_auth_cache, m_fingerprint_computer),
          m_rate_limiter(m_cfg.auth.rps_limit, m_cfg.auth.rate_limit_window_ms),
          m_gate(m_auth_service, m_rate_limiter, m_ws_connection_limiter, nullptr,
                 m_cfg.security.anti_replay.require_for_scopes),
          m_scheduler(to_size_t(m_cfg.queues.high_capacity), to_size_t(m_cfg.queues.low_capacity)),
          m_worker_pool(static_cast<std::size_t>(m_cfg.queues.workers), m_scheduler),
          m_storage_root(std::filesystem::temp_directory_path() /
                         ("dfh-node-http-it-adapter-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
          m_mdbx_path(m_storage_root / "keys.mdbx"),
          m_mdbx_store(std::make_unique<dfh_node::MdbxApiKeyStore>(m_mdbx_path.string())),
          m_disk_monitor(m_storage_root.string(), static_cast<std::uint64_t>(m_cfg.storage.min_free_bytes)),
          m_adapter(adapter), m_router(m_gate, m_scheduler, m_adapter, m_cfg, &m_disk_monitor, m_mdbx_store.get()),
          m_server(m_cfg.http, m_router) {
        std::filesystem::create_directories(m_storage_root);
        m_mdbx_store->open();
        if (auto_start_workers) {
            start_workers();
        }

        m_server.start();
        wait_until_ready();
    }

    ~RunningHttpNodeWithAdapter() {
        m_server.shutdown();
        m_worker_pool.shutdown();
        m_scheduler.shutdown();
    }

    HttpResponse request(const std::string &method, const std::string &path, const std::string &body = "",
                         bool with_token = true, long timeout_seconds = 5,
                         const SwsHeaders &extra_headers = SwsHeaders()) const {
        SwsClient client("127.0.0.1:" + std::to_string(m_cfg.http.port));
        client.config.timeout = timeout_seconds;

        SwsHeaders headers = extra_headers;
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

    int port() const { return m_cfg.http.port; }
    const std::string &token() const { return m_token; }

    void start_workers() {
        if (m_workers_started || m_cfg.queues.workers <= 0) {
            return;
        }
        m_worker_pool.start();
        m_workers_started = true;
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
    dfh_node::UnifiedGate m_gate;
    dfh_node::TaskScheduler m_scheduler;
    dfh_node::WorkerPool m_worker_pool;
    std::filesystem::path m_storage_root;
    std::filesystem::path m_mdbx_path;
    std::unique_ptr<dfh_node::MdbxApiKeyStore> m_mdbx_store;
    dfh_node::DiskMonitor m_disk_monitor;
    dfh_node::IDfhAdapter &m_adapter;
    dfh_node::transport::HttpRouter m_router;
    dfh_node::transport::HttpServer m_server;
    bool m_workers_started{false};
};

std::string make_ingest_body(const std::string &provider = "binance", const std::string &symbol = "BTCUSDT",
                             const std::string &source = "spot", const std::string &tf = "ticks",
                             const std::string &block_ts = "1704067200000",
                             const std::string &payload_base64 = "AQID") {
    return std::string("[{\"key\":{\"provider\":\"") + provider + "\",\"symbol\":\"" + symbol + "\",\"source\":\"" +
           source + "\",\"tf\":\"" + tf + "\",\"block_ts\":" + block_ts + "},\"payload_base64\":\"" + payload_base64 +
           "\"}]";
}

void test_success_endpoints_and_unauthorized() {
    auto cfg = make_base_config();
    RunningHttpNode node(std::move(cfg), "token-success");

    const std::string ingest_body =
        R"([{"key":{"provider":"binance","symbol":"BTCUSDT","source":"spot","tf":"ticks","block_ts":1704067200000},)"
        R"("payload_base64":"AQID"}])";
    SwsHeaders ingest_headers;
    ingest_headers.emplace("Content-Type", "application/json");
    const auto ingest = node.request("POST", "/v1/ingest", ingest_body, true, 5, ingest_headers);
    CHECK_EQ(ingest.status, 200);

    const auto ingest_json = nlohmann::json::parse(ingest.body);
    CHECK(ingest_json.contains("results"));
    CHECK_EQ(ingest_json.at("results").size(), static_cast<std::size_t>(1));

    const std::string history_csv_path =
        "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1704067200000&to_ms=1704070800000"
        "&format=csv";
    const auto history_csv = node.request("GET", history_csv_path);
    CHECK_EQ(history_csv.status, 200);
    CHECK(header_contains(history_csv.headers, "Content-Type", "text/csv"));
    CHECK(header_contains(history_csv.headers, "Content-Disposition", "history.csv"));

    const std::string history_dfhbin_path =
        "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1704067200000&to_ms=1704070800000"
        "&format=dfhbin";
    const auto history_dfhbin = node.request("GET", history_dfhbin_path);
    CHECK_EQ(history_dfhbin.status, 200);
    CHECK(header_contains(history_dfhbin.headers, "Content-Type", "application/octet-stream"));
    CHECK(header_contains(history_dfhbin.headers, "Content-Disposition", "history.dfhbin"));
    CHECK_EQ(history_dfhbin.body.size(), static_cast<std::size_t>(3));
    CHECK_EQ(static_cast<unsigned char>(history_dfhbin.body[0]), static_cast<unsigned char>(1));
    CHECK_EQ(static_cast<unsigned char>(history_dfhbin.body[1]), static_cast<unsigned char>(2));
    CHECK_EQ(static_cast<unsigned char>(history_dfhbin.body[2]), static_cast<unsigned char>(3));

    const auto status = node.request("GET", "/v1/status");
    CHECK_EQ(status.status, 200);
    const auto status_json = nlohmann::json::parse(status.body);
    CHECK(status_json.contains("node_id"));
    CHECK(status_json.contains("version"));
    CHECK(status_json.contains("queues"));
    CHECK(status_json.contains("disk_free_bytes"));
    CHECK(status_json.contains("disk_low"));
    CHECK(status_json.contains("mdbx_keys_active"));
    CHECK(status_json.contains("auth_fail_count"));
    CHECK(status_json.contains("rate_limit_reject_count"));
    CHECK(status_json.contains("anti_replay_reject_count"));
    CHECK(status_json.contains("connection_limit_reject_count"));
    CHECK(status_json.contains("ws_active_connections_total"));

    const auto unauthorized = node.request("GET", "/v1/status", "", false);
    CHECK_EQ(unauthorized.status, 401);
}

void test_status_counts_only_active_mdbx_keys() {
    auto cfg = make_base_config();
    RunningHttpNode node(std::move(cfg), "token-status-active");

    auto active = dfh_node::MdbxKeyRecord{};
    active.id = "active-id";
    active.name = "active-name";
    active.fingerprint = "active-fingerprint";
    active.scope_mask = dfh_node::to_scope_mask(dfh_node::Scope::Read);
    active.created_at_ms = 1000;
    active.updated_at_ms = 1000;

    auto revoked = active;
    revoked.id = "revoked-id";
    revoked.name = "revoked-name";
    revoked.fingerprint = "revoked-fingerprint";
    revoked.revoked = true;

    auto expired = active;
    expired.id = "expired-id";
    expired.name = "expired-name";
    expired.fingerprint = "expired-fingerprint";
    expired.expires_at_ms = 1;

    node.mdbx_store().put(active);
    node.mdbx_store().put(revoked);
    node.mdbx_store().put(expired);

    const auto status = node.request("GET", "/v1/status");
    CHECK_EQ(status.status, 200);
    CHECK_EQ(nlohmann::json::parse(status.body).at("mdbx_keys_active").get<std::uint64_t>(), 1U);
}

void test_ingest_duplicate_returns_ignore_status() {
    auto cfg = make_base_config();
    RunningHttpNode node(std::move(cfg), "token-duplicate");

    const std::string ingest_body = make_ingest_body();
    SwsHeaders ingest_headers;
    ingest_headers.emplace("Content-Type", "application/json");

    const auto first = node.request("POST", "/v1/ingest", ingest_body, true, 5, ingest_headers);
    const auto second = node.request("POST", "/v1/ingest", ingest_body, true, 5, ingest_headers);
    CHECK_EQ(first.status, 200);
    CHECK_EQ(second.status, 200);

    const auto first_json = nlohmann::json::parse(first.body);
    const auto second_json = nlohmann::json::parse(second.body);
    CHECK_EQ(first_json.at("results").at(0).at("status").get<std::string>(), "ok");
    CHECK_EQ(second_json.at("results").at(0).at("status").get<std::string>(), "ignore");
}

void test_validation_and_not_found_errors() {
    auto cfg = make_base_config();
    cfg.http.max_payload_bytes = 256;
    RunningHttpNode node(std::move(cfg), "token-validation");

    SwsHeaders ingest_headers;
    ingest_headers.emplace("Content-Type", "application/json");

    const auto too_large = node.request("POST", "/v1/ingest", std::string(300, 'x'), true, 5, ingest_headers);
    CHECK_EQ(too_large.status, 413);
    CHECK_EQ(nlohmann::json::parse(too_large.body).at("error").get<std::string>(), "payload_too_large");

    const auto invalid_json = node.request("POST", "/v1/ingest", "{", true, 5, ingest_headers);
    CHECK_EQ(invalid_json.status, 400);
    CHECK_EQ(nlohmann::json::parse(invalid_json.body).at("error").get<std::string>(), "invalid_json");

    SwsHeaders incomplete_ar_headers = ingest_headers;
    incomplete_ar_headers.emplace("X-DFH-Timestamp", "1704067200000");
    const auto incomplete_ar = node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, incomplete_ar_headers);
    CHECK_EQ(incomplete_ar.status, 400);
    CHECK_EQ(nlohmann::json::parse(incomplete_ar.body).at("error").get<std::string>(), "missing_anti_replay_headers");

    const auto history_invalid =
        node.request("GET", "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1");
    CHECK_EQ(history_invalid.status, 400);
    CHECK_EQ(nlohmann::json::parse(history_invalid.body).at("error").get<std::string>(), "invalid_query_param");

    const auto history_unauthorized = node.request(
        "GET", "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1&to_ms=2", "", false);
    CHECK_EQ(history_unauthorized.status, 401);

    const auto not_found_get = node.request("GET", "/v1/unknown");
    const auto not_found_post = node.request("POST", "/v1/unknown", "{}", true, 5, ingest_headers);
    const auto not_found_put = node.request("PUT", "/v1/unknown");
    const auto not_found_delete = node.request("DELETE", "/v1/unknown");
    CHECK_EQ(not_found_get.status, 404);
    CHECK_EQ(not_found_post.status, 404);
    CHECK_EQ(not_found_put.status, 404);
    CHECK_EQ(not_found_delete.status, 404);
}

void test_history_response_limits_and_anti_replay() {
    auto cfg = make_base_config();
    cfg.http.history_max_bytes = 2;
    RunningHttpNode node(std::move(cfg), "token-history-limits");

    SwsHeaders ingest_headers;
    ingest_headers.emplace("Content-Type", "application/json");
    ingest_headers.emplace("X-DFH-Timestamp", "1704067200000");
    ingest_headers.emplace("X-DFH-Nonce", "0011223344556677");
    ingest_headers.emplace("X-DFH-Signature", std::string(64, 'a'));
    const auto ingest = node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, ingest_headers);
    CHECK_EQ(ingest.status, 200);

    SwsHeaders history_headers;
    history_headers.emplace("X-DFH-Timestamp", "1704067200001");
    history_headers.emplace("X-DFH-Nonce", "8899aabbccddeeff");
    history_headers.emplace("X-DFH-Signature", std::string(64, 'b'));

    const std::string history_csv_path =
        "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1704067200000&to_ms=1704070800000"
        "&format=csv";
    const auto history_csv = node.request("GET", history_csv_path, "", true, 5, history_headers);
    CHECK_EQ(history_csv.status, 413);
    CHECK_EQ(nlohmann::json::parse(history_csv.body).at("error").get<std::string>(), "response_too_large");

    const std::string history_dfhbin_path =
        "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1704067200000&to_ms=1704070800000"
        "&format=dfhbin";
    const auto history_dfhbin = node.request("GET", history_dfhbin_path, "", true, 5, history_headers);
    CHECK_EQ(history_dfhbin.status, 413);
    CHECK_EQ(nlohmann::json::parse(history_dfhbin.body).at("error").get<std::string>(), "response_too_large");

    SwsHeaders incomplete_ar_headers;
    incomplete_ar_headers.emplace("X-DFH-Nonce", "0011223344556677");
    const auto incomplete_ar = node.request("GET", history_csv_path, "", true, 5, incomplete_ar_headers);
    CHECK_EQ(incomplete_ar.status, 400);
    CHECK_EQ(nlohmann::json::parse(incomplete_ar.body).at("error").get<std::string>(), "missing_anti_replay_headers");
}

void test_history_csv_escaping_and_m1_timeframe() {
    auto cfg = make_base_config();
    RunningHttpNode node(std::move(cfg), "token-csv-escape");

    SwsHeaders ingest_headers;
    ingest_headers.emplace("Content-Type", "application/json");
    const auto ingest = node.request("POST", "/v1/ingest", make_ingest_body("bin,ance", "BT\\\"CUSDT", "spot", "m1"),
                                     true, 5, ingest_headers);
    CHECK_EQ(ingest.status, 200);

    const std::string history_path =
        "/v1/history?provider=bin,ance&symbol=BT%22CUSDT&source=spot&tf=m1&from_ms=1704067200000&to_ms=1704067260000"
        "&format=csv";
    const auto history = node.request("GET", history_path);
    CHECK_EQ(history.status, 200);
    CHECK_NE(history.body.find("\"bin,ance\""), std::string::npos);
    CHECK_NE(history.body.find("\"BT\"\"CUSDT\""), std::string::npos);
    CHECK_NE(history.body.find(",m1,"), std::string::npos);
}

void test_http_server_lifecycle_idempotent() {
    auto cfg = make_base_config();
    RunningHttpNode node(std::move(cfg), "token-lifecycle");

    CHECK(static_cast<bool>(node.executor()));
    node.start_server_again();

    const auto status = node.request("GET", "/v1/status");
    CHECK_EQ(status.status, 200);

    node.shutdown_server_for_test();
    node.shutdown_server_for_test();
}

void test_queue_overflow_maps_to_503() {
    auto cfg = make_base_config();
    cfg.queues.high_capacity = 0;
    cfg.queues.low_capacity = 1;
    cfg.queues.workers = 0;
    cfg.http.request_timeout_ms = 1000;

    RunningHttpNode node(std::move(cfg), "token-queue");

    const std::string ingest_body =
        R"([{"key":{"provider":"binance","symbol":"BTCUSDT","source":"spot","tf":"ticks","block_ts":1704067200000},)"
        R"("payload_base64":"AQID"}])";
    SwsHeaders ingest_headers;
    ingest_headers.emplace("Content-Type", "application/json");
    const auto response = node.request("POST", "/v1/ingest", ingest_body, true, 5, ingest_headers);
    CHECK_EQ(response.status, 503);

    const auto parsed = nlohmann::json::parse(response.body);
    CHECK_EQ(parsed.at("error").get<std::string>(), "queue_full");
}

void test_timeout_maps_to_504() {
    auto cfg = make_base_config();
    cfg.queues.high_capacity = 1;
    cfg.queues.low_capacity = 1;
    cfg.queues.workers = 1;
    cfg.http.request_timeout_ms = 50;

    RunningHttpNode node(std::move(cfg), "token-timeout", false);

    const std::string ingest_body =
        R"([{"key":{"provider":"binance","symbol":"BTCUSDT","source":"spot","tf":"ticks","block_ts":1704067200000},)"
        R"("payload_base64":"AQID"}])";
    SwsHeaders ingest_headers;
    ingest_headers.emplace("Content-Type", "application/json");
    const auto response = node.request("POST", "/v1/ingest", ingest_body, true, 5, ingest_headers);
    CHECK_EQ(response.status, 504);

    const auto parsed = nlohmann::json::parse(response.body);
    CHECK_EQ(parsed.at("error").get<std::string>(), "timeout");

    // После timeout запускаем воркер и даём дочистить хвост очереди, чтобы не оставлять
    // task с `HttpReplyHandle` в очереди к моменту разрушения фикстуры.
    node.start_workers();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void test_history_worker_error_paths() {
    auto cfg = make_base_config();
    ScriptedHttpAdapter adapter;
    RunningHttpNodeWithAdapter node(std::move(cfg), "token-history-worker-errors", adapter);

    const std::string path =
        "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1704067200000&to_ms=1704070800000";

    adapter.set_mode(ScriptedHttpAdapter::Mode::HistoryNull);
    auto response = node.request("GET", path);
    CHECK_EQ(response.status, 500);
    CHECK_EQ(nlohmann::json::parse(response.body).at("error").get<std::string>(), "internal_error");

    adapter.set_mode(ScriptedHttpAdapter::Mode::HistoryThrow);
    response = node.request("GET", path);
    CHECK_EQ(response.status, 500);
    CHECK_EQ(nlohmann::json::parse(response.body).at("error").get<std::string>(), "internal_error");

    adapter.set_mode(ScriptedHttpAdapter::Mode::HistoryThrowUnknown);
    response = node.request("GET", path);
    CHECK_EQ(response.status, 500);
    CHECK_EQ(nlohmann::json::parse(response.body).at("error").get<std::string>(), "internal_error");
}

void test_http_on_error_handles_null_request() {
    auto cfg = make_base_config();
    const std::string token = "token-http-on-error";
    dfh_node::FingerprintComputer fingerprint_computer(cfg.security.server_secret);
    auto api_keys = make_api_keys(fingerprint_computer, token);
    dfh_node::ConfigApiKeyStore api_key_store(api_keys);
    dfh_node::AuthCache auth_cache(cfg.auth.cache_ttl_ms);
    dfh_node::AuthService auth_service(api_key_store, auth_cache, fingerprint_computer);
    dfh_node::RateLimiter rate_limiter(cfg.auth.rps_limit, cfg.auth.rate_limit_window_ms);
    dfh_node::WsConnectionLimiter ws_connection_limiter(cfg.ws.max_ws_connections_total);
    dfh_node::TaskScheduler scheduler(to_size_t(cfg.queues.high_capacity), to_size_t(cfg.queues.low_capacity));
    dfh_node::FakeDfhAdapter adapter;

    const auto storage_root =
        std::filesystem::temp_directory_path() /
        ("dfh-node-http-on-error-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(storage_root);
    dfh_node::MdbxApiKeyStore mdbx_store((storage_root / "keys.mdbx").string());
    mdbx_store.open();
    dfh_node::DiskMonitor disk_monitor(storage_root.string(), static_cast<std::uint64_t>(cfg.storage.min_free_bytes));
    dfh_node::UnifiedGate gate(auth_service, rate_limiter, ws_connection_limiter, nullptr,
                               cfg.security.anti_replay.require_for_scopes);
    dfh_node::transport::HttpRouter router(gate, scheduler, adapter, cfg, &disk_monitor, &mdbx_store);
    SimpleWeb::Server<SimpleWeb::HTTP> server;
    router.register_all(server);

    const auto error_code = make_error_code(std::errc::connection_reset);
    server.on_error(nullptr, error_code);

    scheduler.shutdown();
}

void test_parallel_requests_complete_without_deadlock() {
    auto cfg = make_base_config();
    cfg.queues.high_capacity = 128;
    cfg.queues.low_capacity = 128;
    cfg.queues.workers = 4;
    cfg.http.request_timeout_ms = 1000;

    RunningHttpNode node(std::move(cfg), "token-parallel");

    const std::string ingest_body =
        R"([{"key":{"provider":"binance","symbol":"BTCUSDT","source":"spot","tf":"ticks","block_ts":1704067200000},)"
        R"("payload_base64":"AQID"}])";
    SwsHeaders ingest_headers;
    ingest_headers.emplace("Content-Type", "application/json");
    const auto ingest = node.request("POST", "/v1/ingest", ingest_body, true, 5, ingest_headers);
    CHECK_EQ(ingest.status, 200);

    const std::string history_path =
        "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1704067200000&to_ms=1704070800000"
        "&format=csv";

    std::vector<std::future<int>> futures;
    futures.reserve(20);
    for (int i = 0; i < 20; ++i) {
        futures.push_back(std::async(std::launch::async, [&node, history_path]() {
            const auto response = node.request("GET", history_path);
            return response.status;
        }));
    }

    for (auto &future : futures) {
        const auto state = future.wait_for(std::chrono::seconds(10));
        CHECK_EQ(state, std::future_status::ready);
        CHECK_EQ(future.get(), 200);
    }
}

} // namespace

int main() {
    test_success_endpoints_and_unauthorized();
    test_status_counts_only_active_mdbx_keys();
    test_ingest_duplicate_returns_ignore_status();
    test_validation_and_not_found_errors();
    test_history_response_limits_and_anti_replay();
    test_history_csv_escaping_and_m1_timeframe();
    test_http_server_lifecycle_idempotent();
    test_queue_overflow_maps_to_503();
    test_timeout_maps_to_504();
    test_history_worker_error_paths();
    test_http_on_error_handles_null_request();
    test_parallel_requests_complete_without_deadlock();
    return 0;
}
