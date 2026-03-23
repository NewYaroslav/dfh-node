/// \file peer_sync_service.cpp
/// \brief Реализация pull-based сервиса межнодовой синхронизации.
/// \details Выполняет outbound HTTP-запросы к peer-нодам, сравнивает
/// метаданные блоков и применяет merge raw `dfhbin` через адаптер.
///
#include "peer_sync_service.hpp"

#include "core/logging.hpp"
#include "core/time_utils.hpp"
#include "security/canonical_request.hpp"
#include "security/sha256_utils.hpp"

#include <LogIt.hpp>
#include <client_http.hpp>
#include <client_https.hpp>
#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include <server_http.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dfh_node {
namespace {

struct PeerEndpoint {
    bool https{false};
    std::string host_port;
};

struct PeerHttpResponse {
    std::string status_code;
    std::string body;
};

std::string timeframe_to_string(const Timeframe tf) {
    switch (tf) {
    case Timeframe::Ticks:
        return "ticks";
    case Timeframe::M1:
        return "m1";
    default:
        return {};
    }
}

std::optional<Timeframe> parse_timeframe(const std::string &value) {
    if (value == "ticks") {
        return Timeframe::Ticks;
    }
    if (value == "m1") {
        return Timeframe::M1;
    }
    return std::nullopt;
}

std::vector<Timeframe> known_timeframes() { return {Timeframe::Ticks, Timeframe::M1}; }

std::string bytes_to_hex(const unsigned char *bytes, const std::size_t size) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(size * 2U);
    for (std::size_t index = 0; index < size; ++index) {
        const unsigned char byte = bytes[index];
        hex.push_back(kDigits[(byte >> 4U) & 0x0fU]);
        hex.push_back(kDigits[byte & 0x0fU]);
    }
    return hex;
}

long timeout_seconds(const std::int64_t timeout_ms) {
    if (timeout_ms <= 0) {
        return 1;
    }

    const std::int64_t rounded = (timeout_ms + 999) / 1000;
    return rounded > 0 ? static_cast<long>(rounded) : 1L;
}

PeerEndpoint parse_peer_endpoint(const std::string &url) {
    constexpr std::string_view http_prefix = "http://";
    constexpr std::string_view https_prefix = "https://";

    PeerEndpoint endpoint;
    std::string_view value(url);
    if (value.compare(0, http_prefix.size(), http_prefix) == 0) {
        value.remove_prefix(http_prefix.size());
    } else if (value.compare(0, https_prefix.size(), https_prefix) == 0) {
        endpoint.https = true;
        value.remove_prefix(https_prefix.size());
    } else {
        throw std::runtime_error("peer url must start with http:// or https://");
    }

    const std::size_t slash = value.find('/');
    endpoint.host_port = std::string(value.substr(0, slash));
    if (endpoint.host_port.empty()) {
        throw std::runtime_error("peer url must contain host");
    }

    return endpoint;
}

std::vector<std::pair<std::string, std::string>> query_pairs_from_string(const std::string &query_string) {
    std::vector<std::pair<std::string, std::string>> pairs;
    const auto query = SimpleWeb::QueryString::parse(query_string);
    pairs.reserve(query.size());
    for (const auto &entry : query) {
        pairs.emplace_back(entry.first, entry.second);
    }
    return pairs;
}

std::vector<BlockMeta> list_local_meta_for_timeframe(IDfhAdapter &adapter, const Timeframe tf) {
    auto request = std::make_unique<ListBlockMetaRequest>();
    request->tf = tf;

    const auto response = adapter.list_block_meta(std::move(request));
    if (!response) {
        throw std::runtime_error("local list_block_meta returned null");
    }
    if (response->status == AdapterStatus::Error) {
        throw std::runtime_error("local list_block_meta failed: " + response->error_code);
    }

    return response->blocks;
}

bool should_download_block(const BlockMeta &peer_block, const std::optional<BlockMeta> &local_block, bool &divergent) {
    divergent = false;
    if (!local_block.has_value()) {
        return true;
    }

    if (peer_block.last_ts > local_block->last_ts) {
        return true;
    }
    if (peer_block.last_ts < local_block->last_ts) {
        return false;
    }

    if (peer_block.record_count > local_block->record_count) {
        return true;
    }
    if (peer_block.record_count < local_block->record_count) {
        return false;
    }

    if (peer_block.hash != local_block->hash) {
        divergent = true;
        return true;
    }

    return false;
}

BlockMeta parse_block_meta_json(const nlohmann::json &json_block) {
    if (!json_block.is_object()) {
        throw std::runtime_error("sync meta block must be object");
    }

    BlockMeta block;
    block.key.provider = json_block.at("provider").get<std::string>();
    block.key.symbol = json_block.at("symbol").get<std::string>();
    block.key.source = json_block.at("source").get<std::string>();
    block.key.block_ts = json_block.at("block_ts").get<std::int64_t>();
    block.first_ts = json_block.at("first_ts").get<std::int64_t>();
    block.last_ts = json_block.at("last_ts").get<std::int64_t>();
    block.record_count = json_block.at("record_count").get<std::size_t>();
    block.updated_at = json_block.at("updated_at").get<std::int64_t>();

    const auto tf = parse_timeframe(json_block.at("tf").get<std::string>());
    if (!tf.has_value()) {
        throw std::runtime_error("sync meta contains unknown timeframe");
    }
    block.key.tf = *tf;

    const std::vector<std::uint8_t> hash_bytes = hex_to_bytes(json_block.at("hash").get<std::string>());
    if (hash_bytes.size() != block.hash.size()) {
        throw std::runtime_error("sync meta contains invalid hash");
    }
    std::copy(hash_bytes.begin(), hash_bytes.end(), block.hash.begin());
    return block;
}

template <typename ClientT>
PeerHttpResponse send_request(ClientT &client, const std::string &method, const std::string &path, const std::string &body,
                              const SimpleWeb::CaseInsensitiveMultimap &headers) {
    const auto response = client.request(method, path, body, headers);
    if (!response) {
        throw std::runtime_error("peer request returned null response");
    }

    PeerHttpResponse result;
    result.status_code = response->status_code;
    result.body = response->content.string();
    return result;
}

PeerHttpResponse request_peer(const config::PeerConfig &peer, const config::Config &cfg, const std::string &method,
                              const std::string &path, const std::string &body,
                              const SimpleWeb::CaseInsensitiveMultimap &headers) {
    const PeerEndpoint endpoint = parse_peer_endpoint(peer.url);
    const long timeout = timeout_seconds(cfg.sync.request_timeout_ms);

    if (endpoint.https) {
        SimpleWeb::Client<SimpleWeb::HTTPS> client(endpoint.host_port);
        client.config.timeout = timeout;
        client.config.timeout_connect = timeout;
        return send_request(client, method, path, body, headers);
    }

    SimpleWeb::Client<SimpleWeb::HTTP> client(endpoint.host_port);
    client.config.timeout = timeout;
    client.config.timeout_connect = timeout;
    return send_request(client, method, path, body, headers);
}

} // namespace

PeerSyncService::PeerSyncService(IDfhAdapter &adapter, const config::Config &cfg, DiskMonitor *disk_monitor)
    : m_adapter(adapter), m_cfg(cfg), m_disk_monitor(disk_monitor) {}

PeerSyncService::~PeerSyncService() { shutdown(); }

void PeerSyncService::start() {
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    m_thread = std::thread([this]() { loop(); });
}

void PeerSyncService::shutdown() {
    const bool was_running = m_running.exchange(false, std::memory_order_acq_rel);
    if (!was_running && !m_thread.joinable()) {
        return;
    }

    m_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool PeerSyncService::is_running() const { return m_running.load(std::memory_order_acquire); }

std::int64_t PeerSyncService::last_attempt_at_ms() const { return m_last_attempt_at_ms.load(std::memory_order_acquire); }

std::int64_t PeerSyncService::last_success_at_ms() const { return m_last_success_at_ms.load(std::memory_order_acquire); }

std::int64_t PeerSyncService::estimated_lag_ms() const {
    const std::int64_t last_success = last_success_at_ms();
    if (last_success == 0) {
        return 0;
    }

    const std::int64_t now_ms = now_epoch_ms();
    return now_ms > last_success ? now_ms - last_success : 0;
}

std::uint64_t PeerSyncService::blocks_downloaded_total() const {
    return m_blocks_downloaded.load(std::memory_order_acquire);
}

std::uint64_t PeerSyncService::blocks_merged_total() const { return m_blocks_merged.load(std::memory_order_acquire); }

std::uint64_t PeerSyncService::blocks_skipped_total() const { return m_blocks_skipped.load(std::memory_order_acquire); }

std::uint64_t PeerSyncService::sync_errors_total() const { return m_sync_errors.load(std::memory_order_acquire); }

std::uint64_t PeerSyncService::divergence_total() const { return m_divergence.load(std::memory_order_acquire); }

void PeerSyncService::sync_once() {
    const std::int64_t attempt_at = now_epoch_ms();
    m_last_attempt_at_ms.store(attempt_at, std::memory_order_release);

    bool had_any_success = false;
    for (const auto &peer : m_cfg.peers) {
        sync_peer(peer, had_any_success);
    }

    if (had_any_success) {
        m_last_success_at_ms.store(now_epoch_ms(), std::memory_order_release);
    }
}

void PeerSyncService::loop() {
    while (m_running.load(std::memory_order_acquire)) {
        sync_once();

        std::unique_lock<std::mutex> lock(m_cv_mutex);
        m_cv.wait_for(lock, std::chrono::milliseconds(m_cfg.sync.pull_interval_ms),
                      [this]() { return !m_running.load(std::memory_order_acquire); });
    }
}

void PeerSyncService::sync_peer(const config::PeerConfig &peer, bool &had_any_success) {
    try {
        std::map<BlockKey, BlockMeta> local_blocks;
        for (const Timeframe tf : known_timeframes()) {
            for (const BlockMeta &block : list_local_meta_for_timeframe(m_adapter, tf)) {
                local_blocks[block.key] = block;
            }
        }

        const std::vector<BlockMeta> peer_blocks = fetch_peer_meta(peer);
        had_any_success = true;

        std::uint64_t downloaded = 0;
        std::uint64_t skipped = 0;
        std::uint64_t errors = 0;
        std::uint64_t divergence = 0;

        std::size_t downloaded_this_cycle = 0;
        const std::size_t max_blocks = m_cfg.sync.max_blocks_per_cycle > 0
                                           ? static_cast<std::size_t>(m_cfg.sync.max_blocks_per_cycle)
                                           : 0U;

        for (const BlockMeta &peer_block : peer_blocks) {
            if (downloaded_this_cycle >= max_blocks) {
                break;
            }

            const auto local_it = local_blocks.find(peer_block.key);
            const std::optional<BlockMeta> local_block =
                local_it == local_blocks.end() ? std::nullopt : std::optional<BlockMeta>(local_it->second);

            bool divergent = false;
            if (!should_download_block(peer_block, local_block, divergent)) {
                m_blocks_skipped.fetch_add(1, std::memory_order_relaxed);
                ++skipped;
                continue;
            }

            if (divergent) {
                m_divergence.fetch_add(1, std::memory_order_relaxed);
                ++divergence;
                DFH_PRINTF_WARN("Sync divergence detected for peer=%s block=%s/%s/%s/%lld",
                                peer.id.c_str(), peer_block.key.provider.c_str(), peer_block.key.symbol.c_str(),
                                peer_block.key.source.c_str(), static_cast<long long>(peer_block.key.block_ts));
            }

            if (m_disk_monitor != nullptr && m_disk_monitor->is_disk_low()) {
                m_sync_errors.fetch_add(1, std::memory_order_relaxed);
                ++errors;
                DFH_PRINTF_WARN("Skip sync block due to disk_low: peer=%s block=%s/%s/%s/%lld",
                                peer.id.c_str(), peer_block.key.provider.c_str(), peer_block.key.symbol.c_str(),
                                peer_block.key.source.c_str(), static_cast<long long>(peer_block.key.block_ts));
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(m_inflight_mutex);
                if (!m_inflight.insert(peer_block.key).second) {
                    continue;
                }
            }

            try {
                const std::vector<std::uint8_t> payload = fetch_peer_block(peer, peer_block.key);

                auto request = std::make_unique<MergeBlockDfhbinRequest>();
                request->key = peer_block.key;
                request->bytes = payload;

                const auto merge_response = m_adapter.merge_block_dfhbin(std::move(request));
                if (!merge_response) {
                    throw std::runtime_error("merge_block_dfhbin returned null");
                }
                if (merge_response->status == AdapterStatus::Error) {
                    throw std::runtime_error("merge_block_dfhbin failed: " + merge_response->error_code);
                }

                m_blocks_downloaded.fetch_add(1, std::memory_order_relaxed);
                m_blocks_merged.fetch_add(1, std::memory_order_relaxed);
                ++downloaded;
                ++downloaded_this_cycle;
            } catch (const std::exception &error) {
                m_sync_errors.fetch_add(1, std::memory_order_relaxed);
                ++errors;
                DFH_PRINTF_ERROR("Sync peer block failed: peer=%s error=%s", peer.id.c_str(), error.what());
            } catch (...) {
                m_sync_errors.fetch_add(1, std::memory_order_relaxed);
                ++errors;
                DFH_PRINTF_ERROR("Sync peer block failed: peer=%s error=unknown", peer.id.c_str());
            }

            {
                std::lock_guard<std::mutex> lock(m_inflight_mutex);
                m_inflight.erase(peer_block.key);
            }
        }

        DFH_PRINTF_INFO("Sync peer finished: peer=%s downloaded=%llu skipped=%llu errors=%llu divergence=%llu",
                        peer.id.c_str(), static_cast<unsigned long long>(downloaded),
                        static_cast<unsigned long long>(skipped), static_cast<unsigned long long>(errors),
                        static_cast<unsigned long long>(divergence));
    } catch (const std::exception &error) {
        m_sync_errors.fetch_add(1, std::memory_order_relaxed);
        DFH_PRINTF_ERROR("Sync peer failed: peer=%s error=%s", peer.id.c_str(), error.what());
    } catch (...) {
        m_sync_errors.fetch_add(1, std::memory_order_relaxed);
        DFH_PRINTF_ERROR("Sync peer failed: peer=%s error=unknown", peer.id.c_str());
    }
}

std::vector<BlockMeta> PeerSyncService::fetch_peer_meta(const config::PeerConfig &peer) {
    std::vector<BlockMeta> result;

    for (const Timeframe tf : known_timeframes()) {
        nlohmann::json request_body;
        request_body["tf"] = timeframe_to_string(tf);
        const std::string body = request_body.dump();
        const std::string body_hash = compute_sha256_hex(body);

        SimpleWeb::CaseInsensitiveMultimap headers;
        headers.emplace("Content-Type", "application/json");
        add_auth_headers(headers, "POST", "/sync/meta", "", body_hash);

        const PeerHttpResponse response = request_peer(peer, m_cfg, "POST", "/sync/meta", body, headers);
        if (response.status_code.substr(0, 3) != "200") {
            throw std::runtime_error("sync meta request failed with status " + response.status_code);
        }

        const nlohmann::json response_json = nlohmann::json::parse(response.body);
        if (!response_json.contains("blocks") || !response_json.at("blocks").is_array()) {
            throw std::runtime_error("sync meta response must contain blocks array");
        }

        for (const auto &json_block : response_json.at("blocks")) {
            result.push_back(parse_block_meta_json(json_block));
            if (result.size() >= static_cast<std::size_t>(m_cfg.sync.meta_max_blocks)) {
                return result;
            }
        }
    }

    return result;
}

std::vector<std::uint8_t> PeerSyncService::fetch_peer_block(const config::PeerConfig &peer, const BlockKey &key) {
    SimpleWeb::CaseInsensitiveMultimap query_fields;
    query_fields.emplace("provider", key.provider);
    query_fields.emplace("symbol", key.symbol);
    query_fields.emplace("source", key.source);
    query_fields.emplace("tf", timeframe_to_string(key.tf));
    query_fields.emplace("block_ts", std::to_string(key.block_ts));
    const std::string query_string = SimpleWeb::QueryString::create(query_fields);

    SimpleWeb::CaseInsensitiveMultimap headers;
    add_auth_headers(headers, "GET", "/sync/block", query_string, compute_sha256_hex(""));

    const PeerHttpResponse response = request_peer(peer, m_cfg, "GET", "/sync/block?" + query_string, "", headers);
    if (response.status_code.substr(0, 3) != "200") {
        throw std::runtime_error("sync block request failed with status " + response.status_code);
    }

    const std::string payload = response.body;
    return std::vector<std::uint8_t>(payload.begin(), payload.end());
}

void PeerSyncService::add_auth_headers(SimpleWeb::CaseInsensitiveMultimap &headers, const std::string &method,
                                       const std::string &path, const std::string &query_string,
                                       const std::string &body_hash) {
    if (m_cfg.sync.outbound_token.empty()) {
        throw std::runtime_error("sync outbound token is not configured");
    }

    const std::string timestamp = std::to_string(now_epoch_ms());

    std::array<unsigned char, 8> nonce_bytes{};
    if (RAND_bytes(nonce_bytes.data(), static_cast<int>(nonce_bytes.size())) != 1) {
        throw std::runtime_error("RAND_bytes failed for sync nonce");
    }
    const std::string nonce = bytes_to_hex(nonce_bytes.data(), nonce_bytes.size());

    const std::string signing_key_hex = compute_signing_key_hex(m_cfg.sync.outbound_token);
    const std::vector<std::uint8_t> signing_key_raw = hex_to_bytes(signing_key_hex);
    if (signing_key_raw.size() != 32U) {
        throw std::runtime_error("sync signing key must be 32 bytes");
    }

    HttpCanonicalInput input;
    input.method = method;
    input.path = path;
    input.query_params = query_pairs_from_string(query_string);
    input.timestamp = timestamp;
    input.nonce = nonce;
    input.body_hash = body_hash;

    headers.emplace("Authorization", "Bearer " + m_cfg.sync.outbound_token);
    headers.emplace("X-DFH-Timestamp", timestamp);
    headers.emplace("X-DFH-Nonce", nonce);
    headers.emplace("X-DFH-Signature", compute_signature(canonicalize_http(input), signing_key_raw.data(), 32));
}

} // namespace dfh_node
