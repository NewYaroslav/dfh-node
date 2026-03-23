/// \file sync_router.cpp
/// \brief Реализация роутера HTTP sync API.
/// \details Содержит синхронные обработчики `/sync/meta`, `/sync/block` и
/// `/sync/status` с обязательным anti-replay для всех sync-endpoint'ов.
///
#include "sync_router.hpp"

#include "http_error_map.hpp"
#include "security/anti_replay_fields.hpp"
#include "security/sha256_utils.hpp"
#include "sync/peer_sync_service.hpp"
#include "sync_dto_parser.hpp"
#include "transport/transport_security_utils.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace dfh_node::transport {
namespace {

std::string timeframe_to_string(const Timeframe tf) {
    switch (tf) {
    case Timeframe::Ticks:
        return "ticks";
    case Timeframe::M1:
        return "m1";
    default:
        return "unknown";
    }
}

std::string bytes_to_hex(const std::array<std::uint8_t, 32> &bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2U);
    for (const std::uint8_t byte : bytes) {
        hex.push_back(kDigits[(byte >> 4U) & 0x0fU]);
        hex.push_back(kDigits[byte & 0x0fU]);
    }
    return hex;
}

void send_response(const SyncRouter::HttpResponse &response, const int status, const std::string &body,
                   const std::string &content_type, SimpleWeb::CaseInsensitiveMultimap extra_headers = {}) {
    extra_headers.emplace("Content-Type", content_type);
    response->write(static_cast<SimpleWeb::StatusCode>(status), body, extra_headers);
    response->send();
}

void send_json(const SyncRouter::HttpResponse &response, const int status, const nlohmann::json &body) {
    send_response(response, status, body.dump(), "application/json");
}

void send_gate_error(const SyncRouter::HttpResponse &response, const GateError &error) {
    const auto [status, body] = gate_error_to_http(error);
    send_response(response, status, body, "application/json");
}

void send_error_code(const SyncRouter::HttpResponse &response, const std::string_view error_code,
                     const std::string_view detail = "") {
    const auto [status, body] = error_code_to_http(error_code, detail);
    send_response(response, status, body, "application/json");
}

bool authorize_request(UnifiedGate &gate, const SyncRouter::HttpRequest &request, const SyncRouter::HttpResponse &resp,
                       const std::string &body_hash) {
    const std::string token = extract_bearer_token(request->header);

    HttpAntiReplayFields ar_fields;
    bool has_any_ar_headers = false;
    const HttpAntiReplayFields *ar_ptr =
        parse_http_anti_replay_fields(request->method, request->path, request->query_string, request->header, body_hash,
                                      ar_fields, has_any_ar_headers);

    if (ar_ptr == nullptr) {
        const std::string detail =
            has_any_ar_headers ? "incomplete anti-replay headers" : "sync endpoint requires anti-replay headers";
        send_error_code(resp, "missing_anti_replay_headers", detail);
        return false;
    }

    const GateResult gate_result = gate.authorize_http(token, TaskKind::Sync, ar_ptr);
    if (const auto *error = std::get_if<GateError>(&gate_result)) {
        send_gate_error(resp, *error);
        return false;
    }

    if (!std::holds_alternative<AuthContext>(gate_result)) {
        send_error_code(resp, "internal_error", "unexpected gate result");
        return false;
    }

    return true;
}

nlohmann::json block_meta_to_json(const BlockMeta &block) {
    nlohmann::json body;
    body["provider"] = block.key.provider;
    body["symbol"] = block.key.symbol;
    body["source"] = block.key.source;
    body["tf"] = timeframe_to_string(block.key.tf);
    body["block_ts"] = block.key.block_ts;
    body["first_ts"] = block.first_ts;
    body["last_ts"] = block.last_ts;
    body["record_count"] = block.record_count;
    body["updated_at"] = block.updated_at;
    body["hash"] = bytes_to_hex(block.hash);
    return body;
}

} // namespace

SyncRouter::SyncRouter(UnifiedGate &gate, IDfhAdapter &adapter, const config::Config &cfg,
                       PeerSyncService *sync_service, DiskMonitor *disk_monitor)
    : m_gate(gate), m_adapter(adapter), m_cfg(cfg), m_sync_service(sync_service), m_disk_monitor(disk_monitor) {}

void SyncRouter::register_all(SimpleWeb::Server<SimpleWeb::HTTP> &server) {
    server.resource["^/sync/meta$"]["POST"] = [this](const HttpResponse &response, const HttpRequest &request) {
        handle_meta(request, response);
    };
    server.resource["^/sync/block$"]["GET"] = [this](const HttpResponse &response, const HttpRequest &request) {
        handle_block(request, response);
    };
    server.resource["^/sync/status$"]["GET"] = [this](const HttpResponse &response, const HttpRequest &request) {
        handle_status(request, response);
    };
}

void SyncRouter::handle_meta(HttpRequest req, HttpResponse resp) {
    const std::string body = req->content.string();
    if (!authorize_request(m_gate, req, resp, compute_sha256_hex(body))) {
        return;
    }

    const auto parsed = parse_sync_meta_body(body);
    if (const auto *error = std::get_if<ParseError>(&parsed)) {
        send_error_code(resp, error->first, error->second);
        return;
    }

    auto adapter_req = std::make_unique<ListBlockMetaRequest>(std::get<ListBlockMetaRequest>(parsed));
    const auto adapter_resp = m_adapter.list_block_meta(std::move(adapter_req));
    if (!adapter_resp || adapter_resp->status == AdapterStatus::Error) {
        const std::string error_code = adapter_resp ? adapter_resp->error_code : "internal_error";
        const auto [status, body_json] = adapter_error_to_http(error_code, "sync meta failed");
        send_response(resp, status, body_json, "application/json");
        return;
    }

    nlohmann::json body_json;
    body_json["blocks"] = nlohmann::json::array();
    for (const BlockMeta &block : adapter_resp->blocks) {
        body_json["blocks"].push_back(block_meta_to_json(block));
    }
    send_json(resp, 200, body_json);
}

void SyncRouter::handle_block(HttpRequest req, HttpResponse resp) {
    if (!authorize_request(m_gate, req, resp, compute_sha256_hex(""))) {
        return;
    }

    const auto parsed = parse_sync_block_query(req->query_string);
    if (const auto *error = std::get_if<ParseError>(&parsed)) {
        send_error_code(resp, error->first, error->second);
        return;
    }

    auto adapter_req = std::make_unique<GetBlockDfhbinRequest>(std::get<GetBlockDfhbinRequest>(parsed));
    const auto adapter_resp = m_adapter.get_block_dfhbin(std::move(adapter_req));
    if (!adapter_resp || adapter_resp->status == AdapterStatus::Error) {
        const std::string error_code = adapter_resp ? adapter_resp->error_code : "internal_error";
        const auto [status, body_json] = adapter_error_to_http(error_code, "sync block failed");
        send_response(resp, status, body_json, "application/json");
        return;
    }

    const std::string payload(adapter_resp->payload.begin(), adapter_resp->payload.end());
    send_response(resp, 200, payload, "application/octet-stream");
}

void SyncRouter::handle_status(HttpRequest req, HttpResponse resp) {
    if (!authorize_request(m_gate, req, resp, compute_sha256_hex(""))) {
        return;
    }

    nlohmann::json body;
    body["sync_enabled"] = m_cfg.sync.enabled;
    body["peers_count"] = m_cfg.peers.size();
    body["disk_low"] = m_disk_monitor != nullptr ? m_disk_monitor->is_disk_low() : false;
    body["last_attempt_at_ms"] = m_sync_service != nullptr ? m_sync_service->last_attempt_at_ms() : 0;
    body["last_success_at_ms"] = m_sync_service != nullptr ? m_sync_service->last_success_at_ms() : 0;
    body["estimated_lag_ms"] = m_sync_service != nullptr ? m_sync_service->estimated_lag_ms() : 0;

    nlohmann::json counters;
    counters["blocks_downloaded_total"] = m_sync_service != nullptr ? m_sync_service->blocks_downloaded_total() : 0;
    counters["blocks_merged_total"] = m_sync_service != nullptr ? m_sync_service->blocks_merged_total() : 0;
    counters["blocks_skipped_total"] = m_sync_service != nullptr ? m_sync_service->blocks_skipped_total() : 0;
    counters["sync_errors_total"] = m_sync_service != nullptr ? m_sync_service->sync_errors_total() : 0;
    counters["divergence_total"] = m_sync_service != nullptr ? m_sync_service->divergence_total() : 0;
    body["counters"] = std::move(counters);

    send_json(resp, 200, body);
}

} // namespace dfh_node::transport
