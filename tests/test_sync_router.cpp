/// \file test_sync_router.cpp
/// \brief Интеграционные тесты для `SyncRouter`.
/// \details Поднимает реальный HTTP-сервер и проверяет auth, anti-replay и
/// выдачу `/sync/meta`, `/sync/block`, `/sync/status`.
///
#include "adapter.hpp"
#include "auth.hpp"
#include "config.hpp"
#include "scheduler.hpp"
#include "security.hpp"
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

#include <atomic>
#include <chrono>
#include <cstdint>
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

class EpochSystemClock final : public dfh_node::IClock {
public:
    std::uint64_t now_ms() const override {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
    }
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

std::int64_t now_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string next_nonce() {
    static std::atomic<std::uint64_t> counter{1};
    constexpr char digits[] = "0123456789abcdef";
    std::uint64_t value = counter.fetch_add(1, std::memory_order_relaxed);
    std::string nonce(16, '0');
    for (int index = 15; index >= 0; --index) {
        nonce[static_cast<std::size_t>(index)] = digits[value & 0x0fU];
        value >>= 4U;
    }
    return nonce;
}

void split_path_and_query(const std::string &url, std::string &path,
                          std::vector<std::pair<std::string, std::string>> &query) {
    path = url;
    query.clear();

    const auto question = url.find('?');
    if (question == std::string::npos) {
        return;
    }

    path = url.substr(0, question);
    const std::string query_string = url.substr(question + 1);
    std::size_t start = 0;
    while (start <= query_string.size()) {
        const std::size_t amp = query_string.find('&', start);
        const std::string token =
            (amp == std::string::npos) ? query_string.substr(start) : query_string.substr(start, amp - start);
        if (!token.empty()) {
            const std::size_t eq = token.find('=');
            const std::string key = token.substr(0, eq);
            const std::string value = (eq == std::string::npos) ? std::string() : token.substr(eq + 1);
            query.emplace_back(key, value);
        }
        if (amp == std::string::npos) {
            break;
        }
        start = amp + 1;
    }
}

SwsHeaders make_anti_replay_headers(const std::string &method, const std::string &url, const std::string &body,
                                    const std::string &token) {
    std::string path;
    std::vector<std::pair<std::string, std::string>> query;
    split_path_and_query(url, path, query);

    const std::string timestamp = std::to_string(now_epoch_ms());
    const std::string nonce = next_nonce();
    const std::string body_hash = dfh_node::compute_sha256_hex(body);

    unsigned char signing_key[32];
    dfh_node::compute_sha256_raw(token, signing_key);

    dfh_node::HttpCanonicalInput input;
    input.method = method;
    input.path = path;
    input.query_params = std::move(query);
    input.timestamp = timestamp;
    input.nonce = nonce;
    input.body_hash = body_hash;

    SwsHeaders headers;
    headers.emplace("X-DFH-Timestamp", timestamp);
    headers.emplace("X-DFH-Nonce", nonce);
    headers.emplace("X-DFH-Signature",
                    dfh_node::compute_signature(dfh_node::canonicalize_http(input), signing_key, 32));
    return headers;
}

dfh_node::config::Config make_config() {
    auto cfg = dfh_node::config::default_config();
    cfg.node_id = "node-sync-it";
    cfg.env = "test";
    cfg.security.server_secret = "0123456789abcdef0123456789abcdef";
    cfg.security.anti_replay.enabled = true;
    cfg.security.anti_replay.max_skew_ms = 5000;
    cfg.security.anti_replay.nonce_ttl_ms = 60000;
    cfg.security.anti_replay.nonce_capacity = 1000;
    cfg.security.anti_replay.require_for_scopes =
        dfh_node::to_scope_mask(dfh_node::Scope::Write) | dfh_node::to_scope_mask(dfh_node::Scope::Sync);
    cfg.auth.rps_limit = 100000;
    cfg.auth.rate_limit_window_ms = 1000;
    cfg.auth.cache_ttl_ms = 60000;
    cfg.http.bind_host = "127.0.0.1";
    cfg.http.port = static_cast<int>(acquire_free_port());
    cfg.http.request_timeout_ms = 1000;
    cfg.http.max_payload_bytes = 1024 * 1024;
    cfg.sync.enabled = true;
    cfg.peers.push_back({"peer-a", "http://127.0.0.1:8080"});
    return cfg;
}

class RunningSyncNode {
public:
    RunningSyncNode()
        : m_cfg(make_config()), m_fingerprint_computer(m_cfg.security.server_secret), m_config_store(make_api_keys()),
          m_auth_cache(m_cfg.auth.cache_ttl_ms), m_auth_service(m_config_store, m_auth_cache, m_fingerprint_computer),
          m_rate_limiter(m_cfg.auth.rps_limit, m_cfg.auth.rate_limit_window_ms),
          m_nonce_store(m_epoch_clock, m_cfg.security.anti_replay.nonce_ttl_ms,
                        m_cfg.security.anti_replay.nonce_capacity),
          m_anti_replay_validator(m_cfg.security.anti_replay, m_epoch_clock, m_nonce_store),
          m_gate(m_auth_service, m_rate_limiter, m_ws_connection_limiter, &m_anti_replay_validator,
                 m_cfg.security.anti_replay.require_for_scopes),
          m_scheduler(static_cast<std::size_t>(m_cfg.queues.high_capacity),
                      static_cast<std::size_t>(m_cfg.queues.low_capacity)),
          m_worker_pool(static_cast<std::size_t>(m_cfg.queues.workers), m_scheduler),
          m_http_router(m_gate, m_scheduler, m_adapter, m_cfg, nullptr, nullptr),
          m_http_server(m_cfg.http, m_http_router), m_sync_router(m_gate, m_adapter, m_cfg, nullptr) {
        seed_adapter();
        m_worker_pool.start();
        m_sync_router.register_all(m_http_server.server());
        m_http_server.start();
        wait_until_ready();
    }

    ~RunningSyncNode() {
        m_http_server.shutdown();
        m_worker_pool.shutdown();
        m_scheduler.shutdown();
    }

    HttpResponse request(const std::string &method, const std::string &url, const std::string &body = "",
                         const std::optional<std::string> &token = std::nullopt,
                         const SwsHeaders &extra_headers = SwsHeaders()) const {
        SwsClient client("127.0.0.1:" + std::to_string(m_cfg.http.port));
        client.config.timeout = 5;

        SwsHeaders headers = extra_headers;
        if (token.has_value()) {
            headers.emplace("Authorization", "Bearer " + *token);
        }
        if (!body.empty()) {
            headers.emplace("Content-Type", "application/json");
        }

        auto response = client.request(method, url, body, headers);
        HttpResponse result;
        result.status = parse_status_code(response->status_code);
        result.body = response->content.string();
        result.headers = response->header;
        return result;
    }

    SwsHeaders sync_ar_headers(const std::string &method, const std::string &url, const std::string &body = "") const {
        return make_anti_replay_headers(method, url, body, m_sync_token);
    }

    SwsHeaders admin_ar_headers(const std::string &method, const std::string &url, const std::string &body = "") const {
        return make_anti_replay_headers(method, url, body, m_admin_token);
    }

    SwsHeaders read_ar_headers(const std::string &method, const std::string &url, const std::string &body = "") const {
        return make_anti_replay_headers(method, url, body, m_read_token);
    }

    const std::string &sync_token() const { return m_sync_token; }
    const std::string &admin_token() const { return m_admin_token; }
    const std::string &read_token() const { return m_read_token; }

private:
    std::vector<dfh_node::config::ApiKeyEntry> make_api_keys() const {
        return {
            {m_fingerprint_computer.compute(m_sync_token), dfh_node::to_scope_mask(dfh_node::Scope::Sync), std::nullopt,
             100000, 10},
            {m_fingerprint_computer.compute(m_admin_token), dfh_node::to_scope_mask(dfh_node::Scope::Admin),
             std::nullopt, 100000, 10},
            {m_fingerprint_computer.compute(m_read_token), dfh_node::to_scope_mask(dfh_node::Scope::Read), std::nullopt,
             100000, 10},
        };
    }

    void seed_adapter() {
        auto req = std::make_unique<dfh_node::MergeBlockDfhbinRequest>();
        req->key.provider = "binance";
        req->key.symbol = "BTCUSDT";
        req->key.source = "spot";
        req->key.tf = dfh_node::Timeframe::Ticks;
        req->key.block_ts = 123;
        req->bytes = {'d', 'f', 'h', 'b', 'i', 'n'};
        const auto resp = m_adapter.merge_block_dfhbin(std::move(req));
        CHECK(resp != nullptr);
        CHECK_EQ(resp->status, dfh_node::AdapterStatus::Ok);
    }

    void wait_until_ready() const {
        for (int attempt = 0; attempt < 100; ++attempt) {
            asio::io_service io_service;
            asio::ip::tcp::socket socket(io_service);
            asio::error_code ec;
            socket.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"),
                                                   static_cast<unsigned short>(m_cfg.http.port)),
                           ec);
            if (!ec) {
                socket.close();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CHECK(false);
    }

    dfh_node::config::Config m_cfg;
    std::string m_sync_token{"sync-token"};
    std::string m_admin_token{"admin-token"};
    std::string m_read_token{"read-token"};
    EpochSystemClock m_epoch_clock;
    dfh_node::FingerprintComputer m_fingerprint_computer;
    dfh_node::ConfigApiKeyStore m_config_store;
    dfh_node::AuthCache m_auth_cache;
    dfh_node::AuthService m_auth_service;
    dfh_node::RateLimiter m_rate_limiter;
    dfh_node::WsConnectionLimiter m_ws_connection_limiter;
    dfh_node::NonceStore m_nonce_store;
    dfh_node::AntiReplayValidator m_anti_replay_validator;
    dfh_node::UnifiedGate m_gate;
    dfh_node::TaskScheduler m_scheduler;
    dfh_node::WorkerPool m_worker_pool;
    dfh_node::FakeDfhAdapter m_adapter;
    dfh_node::transport::HttpRouter m_http_router;
    dfh_node::transport::HttpServer m_http_server;
    dfh_node::transport::SyncRouter m_sync_router;
};

void test_sync_meta_requires_auth_and_scope() {
    RunningSyncNode node;
    const std::string body = "{}";

    const auto missing_ar = node.request("POST", "/sync/meta", body, node.sync_token());
    CHECK_EQ(missing_ar.status, 400);
    CHECK_EQ(nlohmann::json::parse(missing_ar.body).at("error").get<std::string>(), "missing_anti_replay_headers");

    const auto unauthorized =
        node.request("POST", "/sync/meta", body, std::nullopt, node.sync_ar_headers("POST", "/sync/meta", body));
    CHECK_EQ(unauthorized.status, 401);
    CHECK_EQ(nlohmann::json::parse(unauthorized.body).at("error").get<std::string>(), "unauthorized");

    const auto forbidden =
        node.request("POST", "/sync/meta", body, node.read_token(), node.read_ar_headers("POST", "/sync/meta", body));
    CHECK_EQ(forbidden.status, 403);
    CHECK_EQ(nlohmann::json::parse(forbidden.body).at("error").get<std::string>(), "forbidden");
}

void test_sync_meta_success_for_sync_and_admin() {
    RunningSyncNode node;
    const std::string body = R"({"provider":"binance","symbol":"BTCUSDT","source":"spot","tf":"ticks"})";

    const auto sync_ok =
        node.request("POST", "/sync/meta", body, node.sync_token(), node.sync_ar_headers("POST", "/sync/meta", body));
    CHECK_EQ(sync_ok.status, 200);
    const auto sync_json = nlohmann::json::parse(sync_ok.body);
    CHECK(sync_json.contains("blocks"));
    CHECK_EQ(sync_json.at("blocks").size(), 1U);
    CHECK(sync_json.at("blocks")[0].contains("hash"));
    CHECK_EQ(sync_json.at("blocks")[0].at("hash").get<std::string>().size(), 64U);

    const auto admin_ok =
        node.request("POST", "/sync/meta", body, node.admin_token(), node.admin_ar_headers("POST", "/sync/meta", body));
    CHECK_EQ(admin_ok.status, 200);
}

void test_sync_meta_bad_signature_and_replay() {
    RunningSyncNode node;
    const std::string body = "{}";

    auto bad_headers = node.sync_ar_headers("POST", "/sync/meta", body);
    bad_headers.erase("X-DFH-Signature");
    bad_headers.emplace("X-DFH-Signature", "0000000000000000000000000000000000000000000000000000000000000000");
    const auto bad_signature = node.request("POST", "/sync/meta", body, node.sync_token(), bad_headers);
    CHECK_EQ(bad_signature.status, 401);
    CHECK_EQ(nlohmann::json::parse(bad_signature.body).at("error").get<std::string>(), "anti_replay_failed");

    const auto replay_headers = node.sync_ar_headers("POST", "/sync/meta", body);
    const auto first = node.request("POST", "/sync/meta", body, node.sync_token(), replay_headers);
    CHECK_EQ(first.status, 200);
    const auto replay = node.request("POST", "/sync/meta", body, node.sync_token(), replay_headers);
    CHECK_EQ(replay.status, 401);
    CHECK_EQ(nlohmann::json::parse(replay.body).at("error").get<std::string>(), "anti_replay_failed");
}

void test_sync_block_and_status() {
    RunningSyncNode node;

    const auto missing_ar = node.request(
        "GET", "/sync/block?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&block_ts=123", "", node.sync_token());
    CHECK_EQ(missing_ar.status, 400);

    const auto block_ok = node.request(
        "GET", "/sync/block?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&block_ts=123", "", node.sync_token(),
        node.sync_ar_headers("GET", "/sync/block?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&block_ts=123"));
    CHECK_EQ(block_ok.status, 200);
    CHECK_EQ(block_ok.body, "dfhbin");

    const auto block_not_found = node.request(
        "GET", "/sync/block?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&block_ts=999", "", node.sync_token(),
        node.sync_ar_headers("GET", "/sync/block?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&block_ts=999"));
    CHECK_EQ(block_not_found.status, 404);

    const auto block_bad_query =
        node.request("GET", "/sync/block?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks", "", node.sync_token(),
                     node.sync_ar_headers("GET", "/sync/block?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks"));
    CHECK_EQ(block_bad_query.status, 400);

    const auto status_missing_ar = node.request("GET", "/sync/status", "", node.sync_token());
    CHECK_EQ(status_missing_ar.status, 400);

    const auto status_ok =
        node.request("GET", "/sync/status", "", node.sync_token(), node.sync_ar_headers("GET", "/sync/status"));
    CHECK_EQ(status_ok.status, 200);
    const auto status_json = nlohmann::json::parse(status_ok.body);
    CHECK_EQ(status_json.at("sync_enabled").get<bool>(), true);
    CHECK_EQ(status_json.at("peers_count").get<std::size_t>(), 1U);
    CHECK(status_json.contains("disk_low"));
    CHECK(status_json.contains("last_attempt_at_ms"));
    CHECK(status_json.contains("last_success_at_ms"));
    CHECK(status_json.contains("estimated_lag_ms"));
    CHECK(status_json.contains("counters"));
    CHECK(status_json.at("counters").contains("blocks_downloaded_total"));
    CHECK(status_json.at("counters").contains("blocks_merged_total"));
    CHECK(status_json.at("counters").contains("blocks_skipped_total"));
    CHECK(status_json.at("counters").contains("sync_errors_total"));
    CHECK(status_json.at("counters").contains("divergence_total"));
}

} // namespace

int main() {
    test_sync_meta_requires_auth_and_scope();
    test_sync_meta_success_for_sync_and_admin();
    test_sync_meta_bad_signature_and_replay();
    test_sync_block_and_status();
    return 0;
}
