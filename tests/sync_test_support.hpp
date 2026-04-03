/// \file sync_test_support.hpp
/// \brief Общие вспомогательные сущности для sync-тестов.
/// \details Содержит тестовый адаптер с управляемыми метаданными блоков и
/// минимальный runtime HTTP-ноды для проверки `PeerSyncService`.
///
#pragma once

#include "adapter.hpp"
#include "auth.hpp"
#include "config.hpp"
#include "scheduler.hpp"
#include "security.hpp"
#include "test_helpers.hpp"
#include "transport.hpp"

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
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace sync_test_support {

/// \brief Часы Unix epoch для anti-replay в sync-тестах.
class EpochSystemClock final : public dfh_node::IClock {
public:
    std::uint64_t now_ms() const override {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
    }
};

/// \brief Возвращает текущее Unix-время в миллисекундах.
inline std::int64_t now_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// \brief Выделяет свободный TCP-порт.
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

/// \brief Создаёт временный каталог для теста.
/// \param prefix Префикс имени каталога.
/// \return Полный путь к уникальному каталогу.
inline std::filesystem::path make_temp_dir(const std::string_view prefix) {
    return std::filesystem::temp_directory_path() /
           (std::string(prefix) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

/// \brief Тестовый адаптер с управляемыми `last_ts` и `record_count`.
class ScriptedAdapter final : public dfh_node::IDfhAdapter {
public:
    /// \brief Создаёт/обновляет блок с заданными метаданными.
    /// \param key Ключ блока.
    /// \param last_ts Значение `last_ts`.
    /// \param record_count Значение `record_count`.
    /// \param marker Тестовый маркер внутри payload.
    void set_block(const dfh_node::BlockKey &key, const std::int64_t last_ts, const std::size_t record_count,
                   const std::string &marker) {
        const std::vector<std::uint8_t> payload = encode_payload(last_ts, record_count, marker);
        store_block(key, last_ts, record_count, payload);
    }

    /// \brief Возвращает `true`, если блок присутствует.
    /// \param key Ключ блока.
    /// \return Признак наличия блока.
    bool has_block(const dfh_node::BlockKey &key) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_payloads.find(key) != m_payloads.end();
    }

    /// \brief Возвращает маркер payload для указанного блока.
    /// \param key Ключ блока.
    /// \return Маркер или пустая строка, если блок отсутствует.
    std::string marker_for(const dfh_node::BlockKey &key) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_payloads.find(key);
        if (it == m_payloads.end()) {
            return {};
        }

        std::int64_t last_ts = 0;
        std::size_t record_count = 0;
        std::string marker;
        if (!decode_payload(it->second, last_ts, record_count, marker)) {
            return {};
        }
        return marker;
    }

    /// \brief Возвращает `record_count` для блока.
    /// \param key Ключ блока.
    /// \return `record_count`, либо `0`, если блока нет.
    std::size_t record_count_for(const dfh_node::BlockKey &key) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_meta.find(key);
        return it == m_meta.end() ? 0U : it->second.record_count;
    }

    /// \brief Возвращает `last_ts` для блока.
    /// \param key Ключ блока.
    /// \return `last_ts`, либо `0`, если блока нет.
    std::int64_t last_ts_for(const dfh_node::BlockKey &key) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_meta.find(key);
        return it == m_meta.end() ? 0 : it->second.last_ts;
    }

    /// \brief Возвращает число вызовов `merge_block_dfhbin()`.
    std::size_t merge_calls() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_merge_calls;
    }

    std::unique_ptr<dfh_node::IngestResponse> ingest_structured(std::unique_ptr<dfh_node::IngestRequest> req) override {
        auto resp = std::make_unique<dfh_node::IngestResponse>();
        if (!req) {
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "invalid_argument";
            return resp;
        }

        std::int64_t last_ts = 0;
        std::size_t record_count = 0;
        std::string marker;
        if (!decode_payload(req->payload, last_ts, record_count, marker)) {
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "invalid_argument";
            return resp;
        }

        store_block(req->key, last_ts, record_count, req->payload);
        resp->status = dfh_node::AdapterStatus::Ok;
        return resp;
    }

    std::unique_ptr<dfh_node::MergeBlockDfhbinResponse>
    merge_block_dfhbin(std::unique_ptr<dfh_node::MergeBlockDfhbinRequest> req) override {
        auto resp = std::make_unique<dfh_node::MergeBlockDfhbinResponse>();
        if (!req) {
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "invalid_argument";
            return resp;
        }

        std::int64_t last_ts = 0;
        std::size_t record_count = 0;
        std::string marker;
        if (!decode_payload(req->bytes, last_ts, record_count, marker)) {
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "invalid_argument";
            return resp;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_merge_calls;
        }
        store_block(req->key, last_ts, record_count, req->bytes);
        resp->status = dfh_node::AdapterStatus::Ok;
        return resp;
    }

    std::unique_ptr<dfh_node::QueryHistoryResponse>
    query_history(std::unique_ptr<dfh_node::QueryHistoryRequest> req) override {
        auto resp = std::make_unique<dfh_node::QueryHistoryResponse>();
        if (!req) {
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "invalid_argument";
            return resp;
        }

        resp->status = dfh_node::AdapterStatus::Ok;
        return resp;
    }

    std::unique_ptr<dfh_node::GetBlockDfhbinResponse>
    get_block_dfhbin(std::unique_ptr<dfh_node::GetBlockDfhbinRequest> req) override {
        auto resp = std::make_unique<dfh_node::GetBlockDfhbinResponse>();
        if (!req) {
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "invalid_argument";
            return resp;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_payloads.find(req->key);
        if (it == m_payloads.end()) {
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "not_found";
            return resp;
        }

        resp->status = dfh_node::AdapterStatus::Ok;
        resp->payload = it->second;
        return resp;
    }

    std::unique_ptr<dfh_node::ListBlockMetaResponse>
    list_block_meta(std::unique_ptr<dfh_node::ListBlockMetaRequest> req) override {
        auto resp = std::make_unique<dfh_node::ListBlockMetaResponse>();
        if (!req) {
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "invalid_argument";
            return resp;
        }

        resp->status = dfh_node::AdapterStatus::Ok;
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto &entry : m_meta) {
            const auto &block = entry.second;
            if (!req->provider.empty() && block.key.provider != req->provider) {
                continue;
            }
            if (!req->symbol.empty() && block.key.symbol != req->symbol) {
                continue;
            }
            if (!req->source.empty() && block.key.source != req->source) {
                continue;
            }
            if (block.key.tf != req->tf) {
                continue;
            }
            if (req->from_block_ts.has_value() && block.key.block_ts < *req->from_block_ts) {
                continue;
            }
            if (req->to_block_ts.has_value() && block.key.block_ts > *req->to_block_ts) {
                continue;
            }
            resp->blocks.push_back(block);
        }
        return resp;
    }

    std::unique_ptr<dfh_node::GetBlockHashResponse>
    get_block_hash(std::unique_ptr<dfh_node::GetBlockHashRequest> req) override {
        auto resp = std::make_unique<dfh_node::GetBlockHashResponse>();
        if (!req) {
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "invalid_argument";
            return resp;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_meta.find(req->key);
        if (it == m_meta.end()) {
            resp->status = dfh_node::AdapterStatus::Error;
            resp->error_code = "not_found";
            return resp;
        }

        resp->status = dfh_node::AdapterStatus::Ok;
        resp->hash = it->second.hash;
        return resp;
    }

private:
    static std::vector<std::uint8_t> encode_payload(const std::int64_t last_ts, const std::size_t record_count,
                                                    const std::string &marker) {
        const std::string text = "last_ts=" + std::to_string(last_ts) +
                                 ";record_count=" + std::to_string(record_count) + ";marker=" + marker;
        return std::vector<std::uint8_t>(text.begin(), text.end());
    }

    static bool decode_payload(const std::vector<std::uint8_t> &payload, std::int64_t &last_ts,
                               std::size_t &record_count, std::string &marker) {
        const std::string text(payload.begin(), payload.end());
        constexpr std::string_view last_prefix = "last_ts=";
        constexpr std::string_view count_prefix = ";record_count=";
        constexpr std::string_view marker_prefix = ";marker=";

        const std::size_t count_pos = text.find(count_prefix);
        const std::size_t marker_pos = text.find(marker_prefix);
        if (text.rfind(last_prefix, 0) != 0 || count_pos == std::string::npos || marker_pos == std::string::npos ||
            marker_pos <= count_pos) {
            return false;
        }

        try {
            last_ts = std::stoll(text.substr(last_prefix.size(), count_pos - last_prefix.size()));
            record_count = static_cast<std::size_t>(std::stoull(
                text.substr(count_pos + count_prefix.size(), marker_pos - (count_pos + count_prefix.size()))));
            marker = text.substr(marker_pos + marker_prefix.size());
            return true;
        } catch (...) {
            return false;
        }
    }

    static std::array<std::uint8_t, 32> compute_hash(const std::vector<std::uint8_t> &payload) {
        std::array<std::uint8_t, 32> hash{};
        const std::string text(payload.begin(), payload.end());
        dfh_node::compute_sha256_raw(text, hash.data());
        return hash;
    }

    void store_block(const dfh_node::BlockKey &key, const std::int64_t last_ts, const std::size_t record_count,
                     const std::vector<std::uint8_t> &payload) {
        const auto hash = compute_hash(payload);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_payloads[key] = payload;
        m_meta[key] = dfh_node::BlockMeta{key, key.block_ts, last_ts, record_count, now_epoch_ms(), hash};
    }

    mutable std::mutex m_mutex;
    std::map<dfh_node::BlockKey, std::vector<std::uint8_t>> m_payloads;
    std::map<dfh_node::BlockKey, dfh_node::BlockMeta> m_meta;
    std::size_t m_merge_calls{0};
};

/// \brief Создаёт базовую конфигурацию для sync-тестов.
/// \param port HTTP-порт сервера.
/// \return Конфигурация с включённым anti-replay и permissive лимитами.
inline dfh_node::config::Config make_base_config(const int port) {
    auto cfg = dfh_node::config::default_config();
    cfg.node_id = "sync-test-node";
    cfg.env = "test";
    cfg.http.bind_host = "127.0.0.1";
    cfg.http.port = port;
    cfg.http.request_timeout_ms = 1000;
    cfg.http.max_payload_bytes = 1024 * 1024;
    cfg.ws.port = port + 1;
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
    cfg.queues.high_capacity = 16;
    cfg.queues.low_capacity = 16;
    cfg.queues.workers = 1;
    cfg.sync.enabled = true;
    cfg.sync.request_timeout_ms = 1000;
    cfg.sync.pull_interval_ms = 50;
    cfg.sync.meta_max_blocks = 1000;
    cfg.sync.max_blocks_per_cycle = 1000;
    cfg.sync.max_parallel_downloads = 1;
    return cfg;
}

/// \brief Минимальная HTTP-нода, публикующая sync-endpoint'ы.
class RunningSyncPeer final {
public:
    /// \brief Поднимает HTTP-сервер sync API.
    /// \param sync_token Токен scope `sync`, которым должен пользоваться клиент.
    explicit RunningSyncPeer(const std::string &sync_token)
        : m_storage_root(make_temp_dir("dfh-node-sync-peer")),
          m_cfg(make_base_config(static_cast<int>(acquire_free_port()))), m_disk_monitor(m_storage_root.string(), 0),
          m_fingerprint_computer(m_cfg.security.server_secret), m_config_store(make_api_keys(sync_token)),
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
          m_http_router(m_gate, m_scheduler, m_adapter, m_cfg, &m_disk_monitor, nullptr),
          m_http_server(m_cfg.http, m_http_router), m_sync_router(m_gate, m_adapter, m_cfg, nullptr, &m_disk_monitor) {
        std::filesystem::create_directories(m_storage_root);
        m_worker_pool.start();
        m_sync_router.register_all(m_http_server.server());
        m_http_server.start();
        wait_until_ready();
    }

    ~RunningSyncPeer() {
        m_http_server.shutdown();
        m_worker_pool.shutdown();
        m_scheduler.shutdown();
        std::error_code ec;
        std::filesystem::remove_all(m_storage_root, ec);
    }

    /// \brief Возвращает базовый URL поднятого сервера.
    std::string base_url() const { return "http://127.0.0.1:" + std::to_string(m_cfg.http.port); }

    /// \brief Возвращает адаптер сервера для заполнения тестовыми блоками.
    ScriptedAdapter &adapter() { return m_adapter; }

private:
    std::vector<dfh_node::config::ApiKeyEntry> make_api_keys(const std::string &sync_token) const {
        return {{m_fingerprint_computer.compute(sync_token), dfh_node::to_scope_mask(dfh_node::Scope::Sync),
                 std::nullopt, 100000, 10}};
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

    std::filesystem::path m_storage_root;
    dfh_node::config::Config m_cfg;
    dfh_node::DiskMonitor m_disk_monitor;
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
    ScriptedAdapter m_adapter;
    dfh_node::transport::HttpRouter m_http_router;
    dfh_node::transport::HttpServer m_http_server;
    dfh_node::transport::SyncRouter m_sync_router;
};

/// \brief Создаёт конфигурацию клиента `PeerSyncService`.
/// \param peer_url URL peer-ноды.
/// \param outbound_token Токен для исходящих sync-запросов.
/// \param storage_root Путь локального каталога хранения.
/// \return Настроенная конфигурация клиента.
inline dfh_node::config::Config make_client_config(const std::string &peer_url, const std::string &outbound_token,
                                                   const std::filesystem::path &storage_root) {
    auto cfg = make_base_config(static_cast<int>(acquire_free_port()));
    cfg.storage.path = storage_root.string();
    cfg.sync.outbound_token = outbound_token;
    cfg.peers = {{"peer-a", peer_url}};
    return cfg;
}

/// \brief Возвращает типовой ключ блока для sync-тестов.
inline dfh_node::BlockKey make_test_key() {
    dfh_node::BlockKey key;
    key.provider = "binance";
    key.symbol = "BTCUSDT";
    key.source = "spot";
    key.tf = dfh_node::Timeframe::Ticks;
    key.block_ts = 1700000000000LL;
    return key;
}

} // namespace sync_test_support
