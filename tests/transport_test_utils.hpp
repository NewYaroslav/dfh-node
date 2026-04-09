/// \file transport_test_utils.hpp
/// \brief Общие helper-утилиты для transport hardening/integration тестов.
/// \details Содержит минимальные фикстуры HTTP/WS-ноды, клиента и утилиты
/// подписи anti-replay без изменения production-кода.
///
#pragma once

#include "adapter.hpp"
#include "auth.hpp"
#include "config.hpp"
#include "scheduler.hpp"
#include "security.hpp"
#include "test_helpers.hpp"
#include "transport.hpp"

#include <client_http.hpp>
#include <client_ws.hpp>
#include <nlohmann/json.hpp>

#ifdef USE_STANDALONE_ASIO
#include <asio.hpp>
#include <asio/ip/tcp.hpp>
#else
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
namespace asio = boost::asio;
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace test_support {

using HttpClient = SimpleWeb::Client<SimpleWeb::HTTP>;
using HttpHeaders = SimpleWeb::CaseInsensitiveMultimap;
using WsClient = SimpleWeb::SocketClient<SimpleWeb::WS>;
using WsConnection = WsClient::Connection;
using WsInMessage = WsClient::InMessage;

struct HttpResponse {
    int status{0};
    std::string body;
    HttpHeaders headers;
};

struct WsFrame {
    std::uint8_t opcode{0};
    std::string payload;

    bool is_binary() const { return (opcode & 0x0fU) == 0x02U; }
};

struct WsCloseEvent {
    int status{0};
    std::string reason;
};

class EpochSystemClock final : public dfh_node::IClock {
public:
    std::uint64_t now_ms() const override {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
    }
};

inline int parse_status_code(const std::string &value) {
    CHECK(value.size() >= 3);
    return std::stoi(value.substr(0, 3));
}

inline bool header_contains(const HttpHeaders &headers, const std::string &name, const std::string &expected_value) {
    const auto range = headers.equal_range(name);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second.find(expected_value) != std::string::npos) {
            return true;
        }
    }
    return false;
}

inline unsigned short acquire_free_port() {
    asio::io_service io_service;
    asio::ip::tcp::acceptor acceptor(io_service);
    acceptor.open(asio::ip::tcp::v4());
    acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    const unsigned short port = acceptor.local_endpoint().port();
    acceptor.close();
    return port;
}

inline std::int64_t now_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

inline std::size_t to_size_t(const std::int64_t value) {
    if (value <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(value);
}

inline dfh_node::ScopeMask all_scopes_mask() {
    return dfh_node::to_scope_mask(dfh_node::Scope::Read) | dfh_node::to_scope_mask(dfh_node::Scope::Write) |
           dfh_node::to_scope_mask(dfh_node::Scope::Admin) | dfh_node::to_scope_mask(dfh_node::Scope::Sync);
}

inline std::vector<dfh_node::config::ApiKeyEntry> make_api_keys(const dfh_node::FingerprintComputer &computer,
                                                                const std::string &token,
                                                                const dfh_node::ScopeMask scopes = all_scopes_mask(),
                                                                const std::int64_t ws_max_connections = 10,
                                                                const std::int64_t rps_limit = 100000) {
    return {
        dfh_node::config::ApiKeyEntry{
            computer.compute(token),
            scopes,
            std::nullopt,
            rps_limit,
            ws_max_connections,
        },
    };
}

inline dfh_node::config::Config make_http_base_config() {
    auto cfg = dfh_node::config::default_config();
    cfg.node_id = "node-http-hardening";
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

inline dfh_node::config::Config make_ws_base_config() {
    auto cfg = dfh_node::config::default_config();
    cfg.node_id = "node-ws-hardening";
    cfg.env = "dev";
    cfg.security.server_secret = "0123456789abcdef0123456789abcdef";
    cfg.security.anti_replay.enabled = false;
    cfg.security.anti_replay.require_for_scopes = 0;
    cfg.auth.rps_limit = 100000;
    cfg.auth.rate_limit_window_ms = 1000;
    cfg.auth.cache_ttl_ms = 60000;
    cfg.http.bind_host = "127.0.0.1";
    cfg.http.port = static_cast<int>(acquire_free_port());
    cfg.ws.bind_host = "127.0.0.1";
    cfg.ws.port = static_cast<int>(acquire_free_port());
    cfg.ws.max_payload_bytes = 1024 * 1024;
    cfg.ws.request_timeout_ms = 1000;
    cfg.ws.history_max_range_ms = 24LL * 60LL * 60LL * 1000LL;
    cfg.ws.history_max_bytes = 1024 * 1024;
    cfg.storage.min_free_bytes = 0;
    cfg.queues.high_capacity = 64;
    cfg.queues.low_capacity = 64;
    cfg.queues.workers = 2;
    return cfg;
}

inline std::string make_ingest_body(const std::string &provider = "binance", const std::string &symbol = "BTCUSDT",
                                    const std::string &source = "spot", const std::string &tf = "ticks",
                                    const std::string &block_ts = "1704067200000",
                                    const std::string &payload_base64 = "AQID") {
    return std::string("[{\"key\":{\"provider\":\"") + provider + "\",\"symbol\":\"" + symbol + "\",\"source\":\"" +
           source + "\",\"tf\":\"" + tf + "\",\"block_ts\":" + block_ts + "},\"payload_base64\":\"" + payload_base64 +
           "\"}]";
}

inline nlohmann::json make_ingest_structured_control(const std::string &msg_id) {
    return {
        {"op", "ingest"},
        {"msg_id", msg_id},
        {"payload",
         {
             {"provider", "binance"},
             {"symbol", "BTCUSDT"},
             {"source", "spot"},
             {"tf", "ticks"},
             {"block_ts", 1704067200000LL},
             {"payload_base64", "AQID"},
         }},
    };
}

inline nlohmann::json make_history_control(const std::string &msg_id) {
    return {
        {"op", "history"},
        {"msg_id", msg_id},
        {"payload",
         {
             {"provider", "binance"},
             {"symbol", "BTCUSDT"},
             {"source", "spot"},
             {"tf", "ticks"},
             {"from_ms", 1704067200000LL},
             {"to_ms", 1704070800000LL},
         }},
    };
}

inline nlohmann::json make_dfhbin_control(const std::string &msg_id, const std::string &payload_sha256) {
    return {
        {"op", "ingest"},
        {"msg_id", msg_id},
        {"payload_sha256", payload_sha256},
        {"payload",
         {
             {"provider", "binance"},
             {"symbol", "BTCUSDT"},
             {"source", "spot"},
             {"tf", "ticks"},
             {"block_ts", 1704067200000LL},
         }},
    };
}

inline std::array<unsigned char, 32> make_signing_key(const std::string &token) {
    std::array<unsigned char, 32> signing_key{};
    dfh_node::compute_sha256_raw(token, signing_key.data());
    return signing_key;
}

inline std::vector<std::pair<std::string, std::string>> split_query_pairs(const std::string &query_string) {
    std::vector<std::pair<std::string, std::string>> pairs;
    std::size_t pos = 0;
    while (pos <= query_string.size()) {
        const std::size_t amp = query_string.find('&', pos);
        const std::string piece = query_string.substr(pos, amp == std::string::npos ? std::string::npos : (amp - pos));
        if (!piece.empty()) {
            const std::size_t eq = piece.find('=');
            if (eq == std::string::npos) {
                pairs.emplace_back(piece, "");
            } else {
                pairs.emplace_back(piece.substr(0, eq), piece.substr(eq + 1));
            }
        }

        if (amp == std::string::npos) {
            break;
        }
        pos = amp + 1;
    }
    return pairs;
}

inline HttpHeaders make_http_anti_replay_headers(const std::string &token, const std::string &method,
                                                 const std::string &path_with_query, const std::string &body,
                                                 const std::string &timestamp, const std::string &nonce) {
    std::string path = path_with_query;
    std::string query_string;
    const std::size_t query_pos = path_with_query.find('?');
    if (query_pos != std::string::npos) {
        path = path_with_query.substr(0, query_pos);
        query_string = path_with_query.substr(query_pos + 1);
    }

    dfh_node::HttpCanonicalInput input;
    input.method = method;
    input.path = path;
    input.query_params = split_query_pairs(query_string);
    input.timestamp = timestamp;
    input.nonce = nonce;
    input.body_hash = dfh_node::compute_sha256_hex(body);

    const auto signing_key = make_signing_key(token);
    HttpHeaders headers;
    headers.emplace("X-DFH-Timestamp", timestamp);
    headers.emplace("X-DFH-Nonce", nonce);
    headers.emplace("X-DFH-Signature", dfh_node::compute_signature(dfh_node::canonicalize_http(input),
                                                                   signing_key.data(), signing_key.size()));
    return headers;
}

inline void sign_ws_control(nlohmann::json &control, const std::string &token, const std::string &endpoint,
                            const std::string &timestamp, const std::string &nonce,
                            std::string payload_hash = std::string()) {
    if (payload_hash.empty()) {
        if (control.contains("payload")) {
            payload_hash = dfh_node::compute_sha256_hex(control.at("payload").dump());
        } else {
            payload_hash = dfh_node::compute_sha256_hex("");
        }
    }

    dfh_node::WsCanonicalInput input;
    input.endpoint = endpoint;
    input.op = control.at("op").get<std::string>();
    input.msg_id = control.at("msg_id").get<std::string>();
    input.timestamp = timestamp;
    input.nonce = nonce;
    input.payload_hash = payload_hash;

    const auto signing_key = make_signing_key(token);
    control["timestamp"] = timestamp;
    control["nonce"] = nonce;
    control["payload_hash"] = payload_hash;
    control["signature"] =
        dfh_node::compute_signature(dfh_node::canonicalize_ws(input), signing_key.data(), signing_key.size());
}

class RunningHttpNode {
public:
    RunningHttpNode(dfh_node::config::Config cfg, std::string token,
                    const dfh_node::ScopeMask scopes = all_scopes_mask(), const bool auto_start_workers = true)
        : m_cfg(std::move(cfg)), m_token(std::move(token)), m_fingerprint_computer(m_cfg.security.server_secret),
          m_api_key_entries(make_api_keys(m_fingerprint_computer, m_token, scopes)), m_api_key_store(m_api_key_entries),
          m_auth_cache(m_cfg.auth.cache_ttl_ms), m_auth_service(m_api_key_store, m_auth_cache, m_fingerprint_computer),
          m_rate_limiter(m_cfg.auth.rps_limit, m_cfg.auth.rate_limit_window_ms),
          m_nonce_store(m_cfg.security.anti_replay.enabled ? std::make_unique<dfh_node::NonceStore>(
                                                                 m_epoch_clock, m_cfg.security.anti_replay.nonce_ttl_ms,
                                                                 m_cfg.security.anti_replay.nonce_capacity)
                                                           : nullptr),
          m_anti_replay_validator(m_nonce_store ? std::make_unique<dfh_node::AntiReplayValidator>(
                                                      m_cfg.security.anti_replay, m_epoch_clock, *m_nonce_store)
                                                : nullptr),
          m_gate(m_auth_service, m_rate_limiter, m_ws_connection_limiter, m_anti_replay_validator.get(),
                 m_cfg.security.anti_replay.require_for_scopes),
          m_scheduler(to_size_t(m_cfg.queues.high_capacity), to_size_t(m_cfg.queues.low_capacity)),
          m_worker_pool(static_cast<std::size_t>(m_cfg.queues.workers), m_scheduler),
          m_storage_root(std::filesystem::temp_directory_path() /
                         ("dfh-node-http-hardening-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
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

    void start_workers() {
        if (m_workers_started || m_cfg.queues.workers <= 0) {
            return;
        }
        m_worker_pool.start();
        m_workers_started = true;
    }

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
    EpochSystemClock m_epoch_clock;
    std::unique_ptr<dfh_node::NonceStore> m_nonce_store;
    std::unique_ptr<dfh_node::AntiReplayValidator> m_anti_replay_validator;
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

class RunningHttpNodeWithAdapter {
public:
    RunningHttpNodeWithAdapter(dfh_node::config::Config cfg, std::string token, dfh_node::IDfhAdapter &adapter,
                               const dfh_node::ScopeMask scopes = all_scopes_mask(),
                               const bool auto_start_workers = true)
        : m_cfg(std::move(cfg)), m_token(std::move(token)), m_fingerprint_computer(m_cfg.security.server_secret),
          m_api_key_entries(make_api_keys(m_fingerprint_computer, m_token, scopes)), m_api_key_store(m_api_key_entries),
          m_auth_cache(m_cfg.auth.cache_ttl_ms), m_auth_service(m_api_key_store, m_auth_cache, m_fingerprint_computer),
          m_rate_limiter(m_cfg.auth.rps_limit, m_cfg.auth.rate_limit_window_ms),
          m_nonce_store(m_cfg.security.anti_replay.enabled ? std::make_unique<dfh_node::NonceStore>(
                                                                 m_epoch_clock, m_cfg.security.anti_replay.nonce_ttl_ms,
                                                                 m_cfg.security.anti_replay.nonce_capacity)
                                                           : nullptr),
          m_anti_replay_validator(m_nonce_store ? std::make_unique<dfh_node::AntiReplayValidator>(
                                                      m_cfg.security.anti_replay, m_epoch_clock, *m_nonce_store)
                                                : nullptr),
          m_gate(m_auth_service, m_rate_limiter, m_ws_connection_limiter, m_anti_replay_validator.get(),
                 m_cfg.security.anti_replay.require_for_scopes),
          m_scheduler(to_size_t(m_cfg.queues.high_capacity), to_size_t(m_cfg.queues.low_capacity)),
          m_worker_pool(static_cast<std::size_t>(m_cfg.queues.workers), m_scheduler),
          m_storage_root(std::filesystem::temp_directory_path() /
                         ("dfh-node-http-adapter-hardening-" +
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

    void start_workers() {
        if (m_workers_started || m_cfg.queues.workers <= 0) {
            return;
        }
        m_worker_pool.start();
        m_workers_started = true;
    }

    const std::string &token() const { return m_token; }

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

class TestWsClient final {
public:
    TestWsClient(const std::string &endpoint, const std::string &token, const std::string &raw_auth_header = "")
        : m_client(endpoint) {
        m_client.config.timeout_request = 5;
        m_client.config.timeout_idle = 5;
        if (!raw_auth_header.empty()) {
            m_client.config.header.emplace("Authorization", raw_auth_header);
        } else if (!token.empty()) {
            m_client.config.header.emplace("Authorization", "Bearer " + token);
        }

        m_client.on_open = [this](const std::shared_ptr<WsConnection> &connection) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_connection = connection;
            m_opened = true;
            m_cv.notify_all();
        };
        m_client.on_message = [this](const std::shared_ptr<WsConnection> &, const std::shared_ptr<WsInMessage> &msg) {
            if (!msg) {
                return;
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            WsFrame frame;
            frame.opcode = msg->fin_rsv_opcode;
            frame.payload = msg->string();
            m_frames.push_back(std::move(frame));
            m_cv.notify_all();
        };
        m_client.on_close = [this](const std::shared_ptr<WsConnection> &, const int status, const std::string &reason) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_close = WsCloseEvent{status, reason};
            m_cv.notify_all();
        };
        m_client.on_error = [this](const std::shared_ptr<WsConnection> &, const SimpleWeb::error_code &error) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_error = error.message();
            m_cv.notify_all();
        };

        m_thread = std::thread([this]() { m_client.start(); });
    }

    ~TestWsClient() { stop(); }

    bool wait_open(const std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        std::unique_lock<std::mutex> lock(m_mutex);
        (void)m_cv.wait_for(lock, timeout, [this]() { return m_opened || m_close.has_value() || m_error.has_value(); });
        return m_opened;
    }

    std::optional<WsFrame> wait_frame(const std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        std::unique_lock<std::mutex> lock(m_mutex);
        const bool ready = m_cv.wait_for(
            lock, timeout, [this]() { return !m_frames.empty() || m_close.has_value() || m_error.has_value(); });
        if (!ready || m_frames.empty()) {
            return std::nullopt;
        }
        WsFrame frame = std::move(m_frames.front());
        m_frames.pop_front();
        return frame;
    }

    std::optional<WsCloseEvent> wait_close(const std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        std::unique_lock<std::mutex> lock(m_mutex);
        const bool ready =
            m_cv.wait_for(lock, timeout, [this]() { return m_close.has_value() || m_error.has_value(); });
        if (!ready) {
            return std::nullopt;
        }
        return m_close;
    }

    std::optional<std::string> wait_error(const std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        std::unique_lock<std::mutex> lock(m_mutex);
        const bool ready = m_cv.wait_for(lock, timeout, [this]() { return m_error.has_value(); });
        if (!ready) {
            return std::nullopt;
        }
        return m_error;
    }

    void send_text(const std::string &payload) {
        auto connection = connection_copy();
        CHECK(connection != nullptr);
        connection->send(payload);
    }

    void send_binary(const std::vector<std::uint8_t> &bytes) {
        auto connection = connection_copy();
        CHECK(connection != nullptr);
        std::string payload;
        if (!bytes.empty()) {
            payload.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        }
        connection->send(payload, nullptr, 130);
    }

    void close_graceful() {
        auto connection = connection_copy();
        CHECK(connection != nullptr);
        connection->send_close(1000, "client-close");
    }

    void stop() {
        if (m_stopped) {
            return;
        }
        m_stopped = true;
        m_client.stop();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

private:
    std::shared_ptr<WsConnection> connection_copy() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_connection;
    }

    WsClient m_client;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::shared_ptr<WsConnection> m_connection;
    std::deque<WsFrame> m_frames;
    std::optional<WsCloseEvent> m_close;
    std::optional<std::string> m_error;
    bool m_opened{false};
    bool m_stopped{false};
};

class RunningWsNode {
public:
    RunningWsNode(dfh_node::config::Config cfg, std::string token, const dfh_node::ScopeMask scopes = all_scopes_mask(),
                  const std::int64_t ws_max_connections = 10, const std::int64_t rps_limit = 100000)
        : m_cfg(std::move(cfg)), m_token(std::move(token)), m_fingerprint_computer(m_cfg.security.server_secret),
          m_api_key_entries(make_api_keys(m_fingerprint_computer, m_token, scopes, ws_max_connections, rps_limit)),
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
          m_worker_pool(static_cast<std::size_t>(std::max(0, m_cfg.queues.workers)), m_scheduler), m_adapter(),
          m_registry(std::make_shared<dfh_node::transport::WsSessionRegistry>()),
          m_gate(m_auth_service, m_rate_limiter, m_ws_connection_limiter, m_anti_replay_validator.get(),
                 m_cfg.security.anti_replay.require_for_scopes),
          m_router(m_gate, m_scheduler, m_adapter, m_cfg, m_registry), m_server(m_cfg.ws, m_router) {
        if (m_cfg.queues.workers > 0) {
            m_worker_pool.start();
        }
        m_server.start();
        wait_until_ready();
    }

    ~RunningWsNode() {
        m_server.shutdown();
        m_worker_pool.shutdown();
        m_scheduler.shutdown();
    }

    std::string endpoint(const std::string &path) const { return "127.0.0.1:" + std::to_string(m_cfg.ws.port) + path; }
    const std::string &token() const { return m_token; }
    std::int64_t active_connections() const { return m_gate.ws_active_connections(); }

private:
    void wait_until_ready() const {
        for (int attempt = 0; attempt < 100; ++attempt) {
            asio::io_service io;
            asio::ip::tcp::socket socket(io);
            asio::error_code ec;
            socket.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"),
                                                   static_cast<unsigned short>(m_cfg.ws.port)),
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
    dfh_node::FakeDfhAdapter m_adapter;
    std::shared_ptr<dfh_node::transport::WsSessionRegistry> m_registry;
    dfh_node::UnifiedGate m_gate;
    dfh_node::transport::WsRouter m_router;
    dfh_node::transport::WsServer m_server;
};

class RunningWsNodeWithAdapter {
public:
    RunningWsNodeWithAdapter(dfh_node::config::Config cfg, std::string token, dfh_node::IDfhAdapter &adapter,
                             const dfh_node::ScopeMask scopes = all_scopes_mask(),
                             const std::int64_t ws_max_connections = 10, const std::int64_t rps_limit = 100000)
        : m_cfg(std::move(cfg)), m_token(std::move(token)), m_fingerprint_computer(m_cfg.security.server_secret),
          m_api_key_entries(make_api_keys(m_fingerprint_computer, m_token, scopes, ws_max_connections, rps_limit)),
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
          m_worker_pool(static_cast<std::size_t>(std::max(0, m_cfg.queues.workers)), m_scheduler), m_adapter(adapter),
          m_registry(std::make_shared<dfh_node::transport::WsSessionRegistry>()),
          m_gate(m_auth_service, m_rate_limiter, m_ws_connection_limiter, m_anti_replay_validator.get(),
                 m_cfg.security.anti_replay.require_for_scopes),
          m_router(m_gate, m_scheduler, m_adapter, m_cfg, m_registry), m_server(m_cfg.ws, m_router) {
        if (m_cfg.queues.workers > 0) {
            m_worker_pool.start();
        }
        m_server.start();
        wait_until_ready();
    }

    ~RunningWsNodeWithAdapter() {
        m_server.shutdown();
        m_worker_pool.shutdown();
        m_scheduler.shutdown();
    }

    std::string endpoint(const std::string &path) const { return "127.0.0.1:" + std::to_string(m_cfg.ws.port) + path; }
    const std::string &token() const { return m_token; }
    std::int64_t active_connections() const { return m_gate.ws_active_connections(); }

private:
    void wait_until_ready() const {
        for (int attempt = 0; attempt < 100; ++attempt) {
            asio::io_service io;
            asio::ip::tcp::socket socket(io);
            asio::error_code ec;
            socket.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"),
                                                   static_cast<unsigned short>(m_cfg.ws.port)),
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
    dfh_node::IDfhAdapter &m_adapter;
    std::shared_ptr<dfh_node::transport::WsSessionRegistry> m_registry;
    dfh_node::UnifiedGate m_gate;
    dfh_node::transport::WsRouter m_router;
    dfh_node::transport::WsServer m_server;
};

inline nlohmann::json wait_json_response(TestWsClient &client,
                                         const std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    const auto frame = client.wait_frame(timeout);
    CHECK(frame.has_value());
    CHECK(!frame->is_binary());
    return nlohmann::json::parse(frame->payload);
}

inline nlohmann::json wait_msgpack_response(TestWsClient &client,
                                            const std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    const auto frame = client.wait_frame(timeout);
    CHECK(frame.has_value());
    CHECK(frame->is_binary());
    std::vector<std::uint8_t> bytes(frame->payload.begin(), frame->payload.end());
    return nlohmann::json::from_msgpack(bytes);
}

inline void send_msgpack_control(TestWsClient &client, const nlohmann::json &payload) {
    const std::vector<std::uint8_t> bytes = nlohmann::json::to_msgpack(payload);
    std::string raw;
    if (!bytes.empty()) {
        raw.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }
    client.send_text(raw);
}

} // namespace test_support
