/// \file test_ops_endpoints.cpp
/// \brief Интеграционные тесты для OpsRouter.
/// \details Проверяет `/health`, `/ready`, `/metrics` и расширенный
/// `/v1/status` на реальном HTTP-сервере.
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
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <thread>
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

dfh_node::config::Config make_config() {
    auto cfg = dfh_node::config::default_config();
    cfg.node_id = "node-ops-it";
    cfg.env = "test";
    cfg.security.server_secret = "0123456789abcdef0123456789abcdef";
    cfg.security.anti_replay.enabled = false;
    cfg.security.anti_replay.require_for_scopes = 0;
    cfg.auth.rps_limit = 100000;
    cfg.auth.rate_limit_window_ms = 1000;
    cfg.auth.cache_ttl_ms = 60000;
    cfg.http.bind_host = "127.0.0.1";
    cfg.http.port = static_cast<int>(acquire_free_port());
    cfg.http.request_timeout_ms = 1000;
    cfg.http.max_payload_bytes = 1024 * 1024;
    cfg.queues.high_capacity = 16;
    cfg.queues.low_capacity = 16;
    cfg.queues.workers = 1;
    return cfg;
}

class RunningOpsNode {
public:
    RunningOpsNode(const bool start_workers, const std::uint64_t min_free_bytes)
        : m_cfg(make_config()),
          m_storage_root(
              std::filesystem::temp_directory_path() /
              ("dfh-node-ops-router-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
          m_mdbx_path(m_storage_root / "keys.mdbx"), m_fingerprint_computer(m_cfg.security.server_secret),
          m_key_store(make_api_keys()), m_auth_cache(m_cfg.auth.cache_ttl_ms),
          m_auth_service(m_key_store, m_auth_cache, m_fingerprint_computer),
          m_rate_limiter(m_cfg.auth.rps_limit, m_cfg.auth.rate_limit_window_ms),
          m_gate(m_auth_service, m_rate_limiter, m_ws_connection_limiter, nullptr, 0),
          m_scheduler(static_cast<std::size_t>(m_cfg.queues.high_capacity),
                      static_cast<std::size_t>(m_cfg.queues.low_capacity)),
          m_worker_pool(static_cast<std::size_t>(m_cfg.queues.workers), m_scheduler),
          m_mdbx_store(m_mdbx_path.string()), m_disk_monitor(m_storage_root.string(), min_free_bytes),
          m_http_router(m_gate, m_scheduler, m_adapter, m_cfg, &m_disk_monitor, &m_mdbx_store),
          m_http_server(m_cfg.http, m_http_router),
          m_ops_router(m_gate, m_disk_monitor, m_mdbx_store, m_scheduler, m_worker_pool, m_cfg) {
        std::filesystem::create_directories(m_storage_root);
        m_mdbx_store.open();
        if (start_workers) {
            m_worker_pool.start();
        }
        m_ops_router.register_all(m_http_server.server());
        m_http_server.start();
        wait_until_ready();
    }

    ~RunningOpsNode() {
        m_http_server.shutdown();
        m_worker_pool.shutdown();
        m_scheduler.shutdown();
    }

    HttpResponse request(const std::string &method, const std::string &path,
                         const std::optional<std::string> &token = std::nullopt) const {
        SwsClient client("127.0.0.1:" + std::to_string(m_cfg.http.port));
        client.config.timeout = 5;

        SwsHeaders headers;
        if (token.has_value()) {
            headers.emplace("Authorization", "Bearer " + *token);
        }

        auto response = client.request(method, path, "", headers);
        HttpResponse result;
        result.status = parse_status_code(response->status_code);
        result.body = response->content.string();
        result.headers = response->header;
        return result;
    }

    const std::string &admin_token() const { return m_admin_token; }
    const std::string &read_token() const { return m_read_token; }

private:
    std::vector<dfh_node::config::ApiKeyEntry> make_api_keys() const {
        return {
            {m_fingerprint_computer.compute(m_admin_token), dfh_node::to_scope_mask(dfh_node::Scope::Admin),
             std::nullopt, 100000, 10},
            {m_fingerprint_computer.compute(m_read_token), dfh_node::to_scope_mask(dfh_node::Scope::Read), std::nullopt,
             100000, 10},
        };
    }

    void wait_until_ready() const {
        for (int attempt = 0; attempt < 100; ++attempt) {
            try {
                const auto response = request("GET", "/health");
                if (response.status == 200) {
                    return;
                }
            } catch (...) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CHECK(false);
    }

    dfh_node::config::Config m_cfg;
    std::filesystem::path m_storage_root;
    std::filesystem::path m_mdbx_path;
    std::string m_admin_token{"admin-token"};
    std::string m_read_token{"read-token"};
    dfh_node::FingerprintComputer m_fingerprint_computer;
    dfh_node::ConfigApiKeyStore m_key_store;
    dfh_node::AuthCache m_auth_cache;
    dfh_node::AuthService m_auth_service;
    dfh_node::RateLimiter m_rate_limiter;
    dfh_node::WsConnectionLimiter m_ws_connection_limiter;
    dfh_node::UnifiedGate m_gate;
    dfh_node::TaskScheduler m_scheduler;
    dfh_node::WorkerPool m_worker_pool;
    dfh_node::MdbxApiKeyStore m_mdbx_store;
    dfh_node::DiskMonitor m_disk_monitor;
    dfh_node::FakeDfhAdapter m_adapter;
    dfh_node::transport::HttpRouter m_http_router;
    dfh_node::transport::HttpServer m_http_server;
    dfh_node::transport::OpsRouter m_ops_router;
};

bool header_contains(const SwsHeaders &headers, const std::string &key, const std::string &fragment) {
    const auto it = headers.find(key);
    if (it == headers.end()) {
        return false;
    }
    return it->second.find(fragment) != std::string::npos;
}

void test_health_and_ready_success() {
    RunningOpsNode node(true, 0);

    const auto health = node.request("GET", "/health");
    CHECK_EQ(health.status, 200);
    CHECK_EQ(nlohmann::json::parse(health.body).at("status").get<std::string>(), "ok");

    const auto ready = node.request("GET", "/ready");
    CHECK_EQ(ready.status, 200);
    CHECK_EQ(nlohmann::json::parse(ready.body).at("ready").get<bool>(), true);
}

void test_ready_reports_disk_low_and_workers_not_running() {
    RunningOpsNode disk_low_node(true, std::numeric_limits<std::uint64_t>::max());
    const auto disk_low = disk_low_node.request("GET", "/ready");
    CHECK_EQ(disk_low.status, 503);
    CHECK_EQ(nlohmann::json::parse(disk_low.body).at("disk_ok").get<bool>(), false);

    RunningOpsNode stopped_workers_node(false, 0);
    const auto stopped = stopped_workers_node.request("GET", "/ready");
    CHECK_EQ(stopped.status, 503);
    CHECK_EQ(nlohmann::json::parse(stopped.body).at("workers_running").get<bool>(), false);
}

void test_metrics_requires_admin_and_status_contains_new_fields() {
    RunningOpsNode node(true, 0);

    const auto unauthorized = node.request("GET", "/metrics");
    CHECK_EQ(unauthorized.status, 401);

    const auto forbidden = node.request("GET", "/metrics", node.read_token());
    CHECK_EQ(forbidden.status, 403);

    const auto metrics = node.request("GET", "/metrics", node.admin_token());
    CHECK_EQ(metrics.status, 200);
    CHECK(header_contains(metrics.headers, "Content-Type", "text/plain; version=0.0.4"));
    CHECK_NE(metrics.body.find("dfh_node_queue_size"), std::string::npos);
    CHECK_NE(metrics.body.find("dfh_node_disk_free_bytes"), std::string::npos);

    (void)node.request("GET", "/ready");
    const auto status = node.request("GET", "/v1/status", node.admin_token());
    CHECK_EQ(status.status, 200);
    const auto status_json = nlohmann::json::parse(status.body);
    CHECK(status_json.contains("disk_free_bytes"));
    CHECK(status_json.contains("disk_low"));
    CHECK(status_json.contains("mdbx_keys_active"));
}

} // namespace

int main() {
    test_health_and_ready_success();
    test_ready_reports_disk_low_and_workers_not_running();
    test_metrics_requires_admin_and_status_contains_new_fields();
    return 0;
}
