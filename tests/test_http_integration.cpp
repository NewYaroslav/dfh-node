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
#include <future>
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
          m_worker_pool(static_cast<std::size_t>(m_cfg.queues.workers), m_scheduler), m_adapter(),
          m_router(m_gate, m_scheduler, m_adapter, m_cfg), m_server(m_cfg.http, m_router) {
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
    dfh_node::FakeDfhAdapter m_adapter;
    dfh_node::transport::HttpRouter m_router;
    dfh_node::transport::HttpServer m_server;
    bool m_workers_started{false};
};

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

    const auto unauthorized = node.request("GET", "/v1/status", "", false);
    CHECK_EQ(unauthorized.status, 401);
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
    test_queue_overflow_maps_to_503();
    test_timeout_maps_to_504();
    test_parallel_requests_complete_without_deadlock();
    return 0;
}
