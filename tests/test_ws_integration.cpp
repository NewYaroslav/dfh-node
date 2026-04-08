/// \file test_ws_integration.cpp
/// \brief Интеграционные тесты WS transport (`WsServer` + `WsRouter`).
/// \details Проверяет upgrade, операции `history`/`ingest`, `dfhbin`,
/// перегруз очередей и anti-replay ошибки.
///
#include "adapter.hpp"
#include "auth.hpp"
#include "config.hpp"
#include "scheduler.hpp"
#include "security.hpp"
#include "test_helpers.hpp"
#include "transport.hpp"

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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using WsClient = SimpleWeb::SocketClient<SimpleWeb::WS>;
using WsConnection = WsClient::Connection;
using WsInMessage = WsClient::InMessage;

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

std::size_t to_size_t(const std::int64_t value) {
    if (value <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(value);
}

std::vector<dfh_node::config::ApiKeyEntry> make_api_keys(const dfh_node::FingerprintComputer &computer,
                                                         const std::string &token, const dfh_node::ScopeMask scopes,
                                                         const std::int64_t ws_max_connections,
                                                         const std::int64_t rps_limit) {
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

dfh_node::config::Config make_base_config() {
    auto cfg = dfh_node::config::default_config();
    cfg.node_id = "node-ws-it";
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
    cfg.queues.high_capacity = 64;
    cfg.queues.low_capacity = 64;
    cfg.queues.workers = 2;
    return cfg;
}

class TestWsClient final {
public:
    TestWsClient(const std::string &endpoint, const std::string &token, const std::string &raw_auth_header = "")
        : m_client(endpoint) {
        m_client.config.timeout_request = 2;
        m_client.config.timeout_idle = 2;
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
        m_client.on_message = [this](const std::shared_ptr<WsConnection> &,
                                     const std::shared_ptr<WsInMessage> &message) {
            if (!message) {
                return;
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            WsFrame frame;
            frame.opcode = message->fin_rsv_opcode;
            frame.payload = message->string();
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

    bool wait_open(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
        std::unique_lock<std::mutex> lock(m_mutex);
        (void)m_cv.wait_for(lock, timeout, [this]() { return m_opened || m_close.has_value() || m_error.has_value(); });
        return m_opened;
    }

    std::optional<WsFrame> wait_frame(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
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

    std::optional<WsCloseEvent> wait_close(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
        std::unique_lock<std::mutex> lock(m_mutex);
        const bool ready =
            m_cv.wait_for(lock, timeout, [this]() { return m_close.has_value() || m_error.has_value(); });
        if (!ready) {
            return std::nullopt;
        }
        return m_close;
    }

    std::optional<std::string> wait_error(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
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

    void close_graceful() {
        auto connection = connection_copy();
        CHECK(connection != nullptr);
        connection->send_close(1000, "client-close");
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

class ScriptedAdapter final : public dfh_node::IDfhAdapter {
public:
    enum class Mode {
        Normal,
        IngestNull,
        IngestErrorInvalidArgument,
        IngestErrorNotFound,
        IngestErrorInternal,
        IngestErrorCustom,
        IngestThrow,
        IngestThrowUnknown,
        HistoryNull,
        HistoryErrorInternal,
        HistoryThrow,
        HistoryThrowUnknown,
        HistoryCustomChunks,
        HistorySlowFirst,
    };

    void set_mode(const Mode mode) { m_mode = mode; }

    std::unique_ptr<dfh_node::IngestResponse> ingest_structured(std::unique_ptr<dfh_node::IngestRequest>) override {
        switch (m_mode) {
        case Mode::IngestNull:
            return nullptr;
        case Mode::IngestErrorInvalidArgument: {
            auto resp = std::make_unique<dfh_node::IngestResponse>();
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "invalid_argument";
            return resp;
        }
        case Mode::IngestErrorNotFound: {
            auto resp = std::make_unique<dfh_node::IngestResponse>();
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "not_found";
            return resp;
        }
        case Mode::IngestErrorInternal: {
            auto resp = std::make_unique<dfh_node::IngestResponse>();
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "internal";
            return resp;
        }
        case Mode::IngestErrorCustom: {
            auto resp = std::make_unique<dfh_node::IngestResponse>();
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "adapter_custom";
            return resp;
        }
        case Mode::IngestThrow:
            throw std::runtime_error("ingest throw");
        case Mode::IngestThrowUnknown:
            throw 7;
        default: {
            auto resp = std::make_unique<dfh_node::IngestResponse>();
            resp->status = dfh_node::AdapterStatus::Ok;
            return resp;
        }
        }
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
        case Mode::HistoryErrorInternal: {
            auto resp = std::make_unique<dfh_node::QueryHistoryResponse>();
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "internal";
            return resp;
        }
        case Mode::HistoryThrow:
            throw std::runtime_error("history throw");
        case Mode::HistoryThrowUnknown:
            throw 11;
        case Mode::HistoryCustomChunks: {
            auto resp = std::make_unique<dfh_node::QueryHistoryResponse>();
            resp->status = dfh_node::AdapterStatus::Ok;

            dfh_node::HistoryChunk c1;
            c1.key.provider = "binance";
            c1.key.symbol = "BTCUSDT";
            c1.key.source = "spot";
            c1.key.tf = dfh_node::Timeframe::M1;
            c1.key.block_ts = 1704067200000LL;

            dfh_node::HistoryChunk c2;
            c2.key.provider = "binance";
            c2.key.symbol = "BTCUSDT";
            c2.key.source = "spot";
            c2.key.tf = static_cast<dfh_node::Timeframe>(999);
            c2.key.block_ts = 1704067260000LL;
            c2.payload = {1, 2, 3};

            resp->chunks.push_back(std::move(c1));
            resp->chunks.push_back(std::move(c2));
            return resp;
        }
        case Mode::HistorySlowFirst: {
            if (m_history_calls.fetch_add(1, std::memory_order_relaxed) == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
            auto resp = std::make_unique<dfh_node::QueryHistoryResponse>();
            resp->status = dfh_node::AdapterStatus::Ok;
            return resp;
        }
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
    std::atomic<int> m_history_calls{0};
};

class RunningWsNode {
public:
    RunningWsNode(dfh_node::config::Config cfg, std::string token, const dfh_node::ScopeMask scopes,
                  const std::int64_t ws_max_connections = 10, const std::int64_t rps_limit = 100000)
        : m_cfg(std::move(cfg)), m_token(std::move(token)), m_fingerprint_computer(m_cfg.security.server_secret),
          m_api_key_entries(make_api_keys(m_fingerprint_computer, m_token, scopes, ws_max_connections, rps_limit)),
          m_api_key_store(m_api_key_entries), m_auth_cache(m_cfg.auth.cache_ttl_ms),
          m_auth_service(m_api_key_store, m_auth_cache, m_fingerprint_computer),
          m_rate_limiter(m_cfg.auth.rps_limit, m_cfg.auth.rate_limit_window_ms),
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
    RunningWsNodeWithAdapter(dfh_node::config::Config cfg, std::string token, const dfh_node::ScopeMask scopes,
                             dfh_node::IDfhAdapter &adapter, const std::int64_t ws_max_connections = 10,
                             const std::int64_t rps_limit = 100000)
        : m_cfg(std::move(cfg)), m_token(std::move(token)), m_fingerprint_computer(m_cfg.security.server_secret),
          m_api_key_entries(make_api_keys(m_fingerprint_computer, m_token, scopes, ws_max_connections, rps_limit)),
          m_api_key_store(m_api_key_entries), m_auth_cache(m_cfg.auth.cache_ttl_ms),
          m_auth_service(m_api_key_store, m_auth_cache, m_fingerprint_computer),
          m_rate_limiter(m_cfg.auth.rps_limit, m_cfg.auth.rate_limit_window_ms),
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

nlohmann::json wait_json_response(TestWsClient &client) {
    const auto frame = client.wait_frame();
    CHECK(frame.has_value());
    CHECK(!frame->is_binary());
    return nlohmann::json::parse(frame->payload);
}

nlohmann::json wait_msgpack_response(TestWsClient &client) {
    const auto frame = client.wait_frame();
    CHECK(frame.has_value());
    CHECK(frame->is_binary());
    std::vector<std::uint8_t> bytes(frame->payload.begin(), frame->payload.end());
    return nlohmann::json::from_msgpack(bytes);
}

nlohmann::json make_ingest_structured_control(const std::string &msg_id) {
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

nlohmann::json make_history_control(const std::string &msg_id) {
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

void send_msgpack_control(TestWsClient &client, const nlohmann::json &payload) {
    const std::vector<std::uint8_t> bytes = nlohmann::json::to_msgpack(payload);
    std::string raw;
    if (!bytes.empty()) {
        raw.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }
    client.send_text(raw);
}

void test_upgrade_without_authorization_closes() {
    auto cfg = make_base_config();
    RunningWsNode node(std::move(cfg), "token-upgrade-no-auth",
                       dfh_node::to_scope_mask(dfh_node::Scope::Read) |
                           dfh_node::to_scope_mask(dfh_node::Scope::Write));

    TestWsClient client(node.endpoint("/ws/json"), "");
    const auto close = client.wait_close();
    const auto error = client.wait_error(std::chrono::milliseconds(500));
    CHECK(close.has_value() || error.has_value());
}

void test_upgrade_with_valid_token_and_limit() {
    auto cfg = make_base_config();
    RunningWsNode node(std::move(cfg), "token-upgrade-ok",
                       dfh_node::to_scope_mask(dfh_node::Scope::Read) | dfh_node::to_scope_mask(dfh_node::Scope::Write),
                       1);

    TestWsClient c1(node.endpoint("/ws/json"), node.token());
    CHECK(c1.wait_open());

    TestWsClient c2(node.endpoint("/ws/json"), node.token());
    const auto close = c2.wait_close();
    CHECK(close.has_value());
    CHECK_EQ(close->status, 1008);
    CHECK_NE(close->reason.find("connection_limited"), std::string::npos);
}

void test_upgrade_rate_limited_and_bad_auth_header() {
    {
        auto cfg = make_base_config();
        cfg.auth.rps_limit = 1;
        cfg.auth.rate_limit_window_ms = 60000;
        RunningWsNode node(
            std::move(cfg), "token-upgrade-rate-limited",
            dfh_node::to_scope_mask(dfh_node::Scope::Read) | dfh_node::to_scope_mask(dfh_node::Scope::Write), 10, 1);

        TestWsClient c1(node.endpoint("/ws/json"), node.token());
        CHECK(c1.wait_open());

        TestWsClient c2(node.endpoint("/ws/json"), node.token());
        const auto close = c2.wait_close();
        CHECK(close.has_value());
        CHECK_EQ(close->status, 1008);
        CHECK_NE(close->reason.find("rate_limited"), std::string::npos);
    }

    {
        auto cfg = make_base_config();
        RunningWsNode node(std::move(cfg), "token-upgrade-bad-header",
                           dfh_node::to_scope_mask(dfh_node::Scope::Read) |
                               dfh_node::to_scope_mask(dfh_node::Scope::Write));

        TestWsClient c(node.endpoint("/ws/json"), "", "Basic abc");
        const auto close = c.wait_close();
        CHECK(close.has_value());
        CHECK_EQ(close->status, 1008);
        CHECK_NE(close->reason.find("unauthorized"), std::string::npos);
    }
}

void test_history_json_and_msgpack() {
    auto cfg = make_base_config();
    RunningWsNode node(std::move(cfg), "token-history",
                       dfh_node::to_scope_mask(dfh_node::Scope::Read) |
                           dfh_node::to_scope_mask(dfh_node::Scope::Write));

    {
        TestWsClient json_client(node.endpoint("/ws/json"), node.token());
        CHECK(json_client.wait_open());

        json_client.send_text(make_ingest_structured_control("ing-1").dump());
        const auto ingest_resp = wait_json_response(json_client);
        CHECK_EQ(ingest_resp.at("ok").get<bool>(), true);
        CHECK_EQ(ingest_resp.at("msg_id").get<std::string>(), "ing-1");

        json_client.send_text(make_history_control("hist-1").dump());
        const auto history_resp = wait_json_response(json_client);
        CHECK_EQ(history_resp.at("ok").get<bool>(), true);
        CHECK_EQ(history_resp.at("msg_id").get<std::string>(), "hist-1");
        CHECK(history_resp.at("data").contains("chunks"));
        CHECK(history_resp.at("data").at("chunks").size() >= static_cast<std::size_t>(1));
    }

    {
        TestWsClient msgpack_client(node.endpoint("/ws/msgpack"), node.token());
        CHECK(msgpack_client.wait_open());
        send_msgpack_control(msgpack_client, make_history_control("hist-2"));
        const auto history_resp = wait_msgpack_response(msgpack_client);
        CHECK_EQ(history_resp.at("ok").get<bool>(), true);
        CHECK_EQ(history_resp.at("msg_id").get<std::string>(), "hist-2");
        CHECK(history_resp.at("data").contains("chunks"));
    }
}

void test_dfhbin_success_and_errors() {
    auto cfg = make_base_config();
    RunningWsNode node(std::move(cfg), "token-dfhbin",
                       dfh_node::to_scope_mask(dfh_node::Scope::Read) |
                           dfh_node::to_scope_mask(dfh_node::Scope::Write));

    TestWsClient client(node.endpoint("/ws/json"), node.token());
    CHECK(client.wait_open());

    const std::vector<std::uint8_t> payload = {1, 2, 3, 4, 5};
    std::string raw(reinterpret_cast<const char *>(payload.data()), payload.size());
    const std::string payload_sha256 = dfh_node::compute_sha256_hex(raw);

    const nlohmann::json control_ok = {
        {"op", "ingest"},
        {"msg_id", "dfh-1"},
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
    client.send_text(control_ok.dump());
    client.send_binary(payload);
    const auto ok = wait_json_response(client);
    CHECK_EQ(ok.at("ok").get<bool>(), true);
    CHECK_EQ(ok.at("msg_id").get<std::string>(), "dfh-1");

    const nlohmann::json control_bad = {
        {"op", "ingest"},
        {"msg_id", "dfh-2"},
        {"payload_sha256", std::string(64, 'a')},
        {"payload",
         {
             {"provider", "binance"},
             {"symbol", "BTCUSDT"},
             {"source", "spot"},
             {"tf", "ticks"},
             {"block_ts", 1704067200000LL},
         }},
    };
    client.send_text(control_bad.dump());
    client.send_binary(payload);
    const auto mismatch = wait_json_response(client);
    CHECK_EQ(mismatch.at("ok").get<bool>(), false);
    CHECK_EQ(mismatch.at("error_code").get<std::string>(), "sha256_mismatch");
    CHECK_EQ(mismatch.at("msg_id").get<std::string>(), "dfh-2");

    client.send_binary(payload);
    const auto unexpected = wait_json_response(client);
    CHECK_EQ(unexpected.at("ok").get<bool>(), false);
    CHECK_EQ(unexpected.at("error_code").get<std::string>(), "unexpected_binary_frame");
}

void test_overload_and_subscribe() {
    {
        auto cfg = make_base_config();
        cfg.queues.high_capacity = 0;
        cfg.queues.low_capacity = 1;
        cfg.queues.workers = 0;
        RunningWsNode node(std::move(cfg), "token-overload-high",
                           dfh_node::to_scope_mask(dfh_node::Scope::Read) |
                               dfh_node::to_scope_mask(dfh_node::Scope::Write));

        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(make_ingest_structured_control("ov-high").dump());
        const auto response = wait_json_response(client);
        CHECK_EQ(response.at("ok").get<bool>(), false);
        CHECK_EQ(response.at("error_code").get<std::string>(), "overload.high_priority_queue_full");
    }

    {
        auto cfg = make_base_config();
        cfg.queues.high_capacity = 1;
        cfg.queues.low_capacity = 0;
        cfg.queues.workers = 0;
        RunningWsNode node(std::move(cfg), "token-overload-low",
                           dfh_node::to_scope_mask(dfh_node::Scope::Read) |
                               dfh_node::to_scope_mask(dfh_node::Scope::Write));

        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(make_history_control("ov-low").dump());
        const auto response = wait_json_response(client);
        CHECK_EQ(response.at("ok").get<bool>(), false);
        CHECK_EQ(response.at("error_code").get<std::string>(), "overload.low_priority_queue_full");
    }

    {
        auto cfg = make_base_config();
        RunningWsNode node(std::move(cfg), "token-subscribe",
                           dfh_node::to_scope_mask(dfh_node::Scope::Read) |
                               dfh_node::to_scope_mask(dfh_node::Scope::Write));

        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        const nlohmann::json subscribe = {
            {"op", "subscribe"},
            {"msg_id", "sub-1"},
            {"payload", nlohmann::json::object()},
        };
        client.send_text(subscribe.dump());
        const auto response = wait_json_response(client);
        CHECK_EQ(response.at("ok").get<bool>(), false);
        CHECK_EQ(response.at("error_code").get<std::string>(), "unsupported_operation");
        CHECK_EQ(response.at("msg_id").get<std::string>(), "sub-1");
    }
}

void test_anti_replay_invalid_signature() {
    auto cfg = make_base_config();
    cfg.security.anti_replay.enabled = true;
    cfg.security.anti_replay.max_skew_ms = 5000;
    cfg.security.anti_replay.nonce_ttl_ms = 60000;
    cfg.security.anti_replay.nonce_capacity = 1000;
    cfg.security.anti_replay.require_for_scopes = dfh_node::to_scope_mask(dfh_node::Scope::Write) |
                                                  dfh_node::to_scope_mask(dfh_node::Scope::Admin) |
                                                  dfh_node::to_scope_mask(dfh_node::Scope::Sync);

    RunningWsNode node(std::move(cfg), "token-ar",
                       dfh_node::to_scope_mask(dfh_node::Scope::Read) |
                           dfh_node::to_scope_mask(dfh_node::Scope::Write));

    TestWsClient client(node.endpoint("/ws/json"), node.token());
    CHECK(client.wait_open());

    const std::string msg_id = "ar-1";
    const std::string payload_hash = std::string(64, 'a');
    const std::string nonce = "0011223344556677";
    const std::string timestamp = std::to_string(now_epoch_ms());
    const std::string signature = std::string(64, '0');

    const nlohmann::json control = {
        {"op", "ingest"},
        {"msg_id", msg_id},
        {"payload_hash", payload_hash},
        {"timestamp", timestamp},
        {"nonce", nonce},
        {"signature", signature},
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
    client.send_text(control.dump());
    const auto response = wait_json_response(client);
    CHECK_EQ(response.at("ok").get<bool>(), false);
    CHECK_EQ(response.at("msg_id").get<std::string>(), msg_id);
    CHECK_EQ(response.at("error_code").get<std::string>(), "anti_replay_failed");
}

void test_invalid_control_message() {
    auto cfg = make_base_config();
    RunningWsNode node(std::move(cfg), "token-invalid-control",
                       dfh_node::to_scope_mask(dfh_node::Scope::Read) |
                           dfh_node::to_scope_mask(dfh_node::Scope::Write));

    {
        TestWsClient json_client(node.endpoint("/ws/json"), node.token());
        CHECK(json_client.wait_open());
        json_client.send_text("{");
        const auto response = wait_json_response(json_client);
        CHECK_EQ(response.at("ok").get<bool>(), false);
        CHECK_EQ(response.at("error_code").get<std::string>(), "invalid_control_message");
    }
}

void test_forbidden_and_rate_limited() {
    {
        auto cfg = make_base_config();
        RunningWsNode node(std::move(cfg), "token-forbidden", dfh_node::to_scope_mask(dfh_node::Scope::Read));

        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(make_ingest_structured_control("forbidden-1").dump());
        const auto response = wait_json_response(client);
        CHECK_EQ(response.at("ok").get<bool>(), false);
        CHECK_EQ(response.at("error_code").get<std::string>(), "forbidden");
    }

    {
        auto cfg = make_base_config();
        cfg.auth.rps_limit = 2;
        cfg.auth.rate_limit_window_ms = 60000;
        RunningWsNode node(
            std::move(cfg), "token-rate-limited",
            dfh_node::to_scope_mask(dfh_node::Scope::Read) | dfh_node::to_scope_mask(dfh_node::Scope::Write), 10, 2);

        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());

        client.send_text(make_history_control("rl-1").dump());
        const auto ok = wait_json_response(client);
        CHECK_EQ(ok.at("ok").get<bool>(), true);

        client.send_text(make_history_control("rl-2").dump());
        const auto limited = wait_json_response(client);
        CHECK_EQ(limited.at("ok").get<bool>(), false);
        CHECK_EQ(limited.at("error_code").get<std::string>(), "rate_limited");
        CHECK_EQ(limited.at("msg_id").get<std::string>(), "rl-2");
    }
}

void test_anti_replay_required_and_empty_fields() {
    {
        auto cfg = make_base_config();
        cfg.security.anti_replay.enabled = false;
        cfg.security.anti_replay.require_for_scopes = dfh_node::to_scope_mask(dfh_node::Scope::Write);
        RunningWsNode node(std::move(cfg), "token-ar-required",
                           dfh_node::to_scope_mask(dfh_node::Scope::Read) |
                               dfh_node::to_scope_mask(dfh_node::Scope::Write));

        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(make_ingest_structured_control("ar-required-1").dump());
        const auto response = wait_json_response(client);
        CHECK_EQ(response.at("ok").get<bool>(), false);
        CHECK_EQ(response.at("error_code").get<std::string>(), "anti_replay_required");
    }

    {
        auto cfg = make_base_config();
        cfg.security.anti_replay.enabled = true;
        cfg.security.anti_replay.max_skew_ms = 5000;
        cfg.security.anti_replay.nonce_ttl_ms = 60000;
        cfg.security.anti_replay.nonce_capacity = 1000;
        cfg.security.anti_replay.require_for_scopes = dfh_node::to_scope_mask(dfh_node::Scope::Write);
        RunningWsNode node(std::move(cfg), "token-ar-missing-fields",
                           dfh_node::to_scope_mask(dfh_node::Scope::Read) |
                               dfh_node::to_scope_mask(dfh_node::Scope::Write));

        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(make_ingest_structured_control("ar-missing-1").dump());
        const auto response = wait_json_response(client);
        CHECK_EQ(response.at("ok").get<bool>(), false);
        CHECK_EQ(response.at("error_code").get<std::string>(), "anti_replay_failed");
    }
}

void test_close_connection_during_task_no_crash() {
    auto cfg = make_base_config();
    cfg.queues.high_capacity = 32;
    cfg.queues.low_capacity = 32;
    cfg.queues.workers = 1;
    RunningWsNode node(std::move(cfg), "token-close-during-task",
                       dfh_node::to_scope_mask(dfh_node::Scope::Read) |
                           dfh_node::to_scope_mask(dfh_node::Scope::Write));

    {
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        for (int i = 0; i < 20; ++i) {
            const std::string msg_id = "close-" + std::to_string(i);
            client.send_text(make_ingest_structured_control(msg_id).dump());
        }
    }

    TestWsClient probe(node.endpoint("/ws/json"), node.token());
    CHECK(probe.wait_open());
    probe.send_text(make_history_control("probe-1").dump());
    const auto response = wait_json_response(probe);
    CHECK_EQ(response.at("ok").get<bool>(), true);
}

void test_close_callback_releases_connection_limit() {
    auto cfg = make_base_config();
    RunningWsNode node(std::move(cfg), "token-close-release",
                       dfh_node::to_scope_mask(dfh_node::Scope::Read) | dfh_node::to_scope_mask(dfh_node::Scope::Write),
                       1);

    TestWsClient c1(node.endpoint("/ws/json"), node.token());
    CHECK(c1.wait_open());
    c1.close_graceful();
    (void)c1.wait_close(std::chrono::milliseconds(1000));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    TestWsClient c2(node.endpoint("/ws/json"), node.token());
    CHECK(c2.wait_open());
}

void test_scripted_adapter_error_paths() {
    auto cfg = make_base_config();
    ScriptedAdapter adapter;
    RunningWsNodeWithAdapter node(
        std::move(cfg), "token-scripted-adapter",
        dfh_node::to_scope_mask(dfh_node::Scope::Read) | dfh_node::to_scope_mask(dfh_node::Scope::Write), adapter);

    TestWsClient client(node.endpoint("/ws/json"), node.token());
    CHECK(client.wait_open());

    adapter.set_mode(ScriptedAdapter::Mode::IngestErrorInvalidArgument);
    client.send_text(make_ingest_structured_control("sa-ing-1").dump());
    auto resp = wait_json_response(client);
    CHECK_EQ(resp.at("error_code").get<std::string>(), "invalid_argument");

    adapter.set_mode(ScriptedAdapter::Mode::IngestErrorNotFound);
    client.send_text(make_ingest_structured_control("sa-ing-2").dump());
    resp = wait_json_response(client);
    CHECK_EQ(resp.at("error_code").get<std::string>(), "not_found");

    adapter.set_mode(ScriptedAdapter::Mode::IngestErrorInternal);
    client.send_text(make_ingest_structured_control("sa-ing-3").dump());
    resp = wait_json_response(client);
    CHECK_EQ(resp.at("error_code").get<std::string>(), "internal_error");

    adapter.set_mode(ScriptedAdapter::Mode::IngestErrorCustom);
    client.send_text(make_ingest_structured_control("sa-ing-4").dump());
    resp = wait_json_response(client);
    CHECK_EQ(resp.at("error_code").get<std::string>(), "adapter_custom");

    adapter.set_mode(ScriptedAdapter::Mode::IngestNull);
    client.send_text(make_ingest_structured_control("sa-ing-5").dump());
    resp = wait_json_response(client);
    CHECK_EQ(resp.at("error_code").get<std::string>(), "internal_error");

    adapter.set_mode(ScriptedAdapter::Mode::IngestThrow);
    client.send_text(make_ingest_structured_control("sa-ing-6").dump());
    resp = wait_json_response(client);
    CHECK_EQ(resp.at("error_code").get<std::string>(), "internal_error");

    adapter.set_mode(ScriptedAdapter::Mode::IngestThrowUnknown);
    client.send_text(make_ingest_structured_control("sa-ing-7").dump());
    resp = wait_json_response(client);
    CHECK_EQ(resp.at("error_code").get<std::string>(), "internal_error");

    adapter.set_mode(ScriptedAdapter::Mode::HistoryNull);
    client.send_text(make_history_control("sa-h-1").dump());
    resp = wait_json_response(client);
    CHECK_EQ(resp.at("error_code").get<std::string>(), "internal_error");

    adapter.set_mode(ScriptedAdapter::Mode::HistoryErrorInternal);
    client.send_text(make_history_control("sa-h-2").dump());
    resp = wait_json_response(client);
    CHECK_EQ(resp.at("error_code").get<std::string>(), "internal_error");

    adapter.set_mode(ScriptedAdapter::Mode::HistoryThrow);
    client.send_text(make_history_control("sa-h-3").dump());
    resp = wait_json_response(client);
    CHECK_EQ(resp.at("error_code").get<std::string>(), "internal_error");

    adapter.set_mode(ScriptedAdapter::Mode::HistoryThrowUnknown);
    client.send_text(make_history_control("sa-h-4").dump());
    resp = wait_json_response(client);
    CHECK_EQ(resp.at("error_code").get<std::string>(), "internal_error");

    adapter.set_mode(ScriptedAdapter::Mode::HistoryCustomChunks);
    client.send_text(make_history_control("sa-h-5").dump());
    resp = wait_json_response(client);
    CHECK_EQ(resp.at("ok").get<bool>(), true);
    CHECK_EQ(resp.at("data").at("chunks").size(), static_cast<std::size_t>(2));
    CHECK_EQ(resp.at("data").at("chunks").at(0).at("key").at("tf").get<std::string>(), "m1");
    CHECK_EQ(resp.at("data").at("chunks").at(1).at("key").at("tf").get<std::string>(), "unknown");
}

void test_history_response_too_large() {
    auto cfg = make_base_config();
    cfg.ws.history_max_bytes = 2;
    ScriptedAdapter adapter;
    adapter.set_mode(ScriptedAdapter::Mode::HistoryCustomChunks);
    RunningWsNodeWithAdapter node(
        std::move(cfg), "token-history-too-large",
        dfh_node::to_scope_mask(dfh_node::Scope::Read) | dfh_node::to_scope_mask(dfh_node::Scope::Write), adapter);

    TestWsClient client(node.endpoint("/ws/json"), node.token());
    CHECK(client.wait_open());

    client.send_text(make_history_control("hist-too-large").dump());
    const auto resp = wait_json_response(client);
    CHECK_EQ(resp.at("ok").get<bool>(), false);
    CHECK_EQ(resp.at("msg_id").get<std::string>(), "hist-too-large");
    CHECK_EQ(resp.at("error_code").get<std::string>(), "response_too_large");
}

void test_history_soft_timeout() {
    auto cfg = make_base_config();
    cfg.ws.request_timeout_ms = 50;
    cfg.queues.workers = 1;
    ScriptedAdapter adapter;
    adapter.set_mode(ScriptedAdapter::Mode::HistorySlowFirst);
    RunningWsNodeWithAdapter node(
        std::move(cfg), "token-history-timeout",
        dfh_node::to_scope_mask(dfh_node::Scope::Read) | dfh_node::to_scope_mask(dfh_node::Scope::Write), adapter);

    TestWsClient client(node.endpoint("/ws/json"), node.token());
    CHECK(client.wait_open());

    client.send_text(make_history_control("hist-slow-1").dump());
    client.send_text(make_history_control("hist-slow-2").dump());

    const auto first = wait_json_response(client);
    const auto second = wait_json_response(client);

    CHECK_EQ(first.at("ok").get<bool>(), true);
    CHECK_EQ(first.at("msg_id").get<std::string>(), "hist-slow-1");
    CHECK_EQ(second.at("ok").get<bool>(), false);
    CHECK_EQ(second.at("msg_id").get<std::string>(), "hist-slow-2");
    CHECK_EQ(second.at("error_code").get<std::string>(), "timeout");
}

} // namespace

int main() {
    test_upgrade_without_authorization_closes();
    test_upgrade_with_valid_token_and_limit();
    test_upgrade_rate_limited_and_bad_auth_header();
    test_history_json_and_msgpack();
    test_dfhbin_success_and_errors();
    test_overload_and_subscribe();
    test_anti_replay_invalid_signature();
    test_invalid_control_message();
    test_forbidden_and_rate_limited();
    test_anti_replay_required_and_empty_fields();
    test_close_connection_during_task_no_crash();
    test_close_callback_releases_connection_limit();
    test_scripted_adapter_error_paths();
    test_history_response_too_large();
    test_history_soft_timeout();
    return 0;
}
