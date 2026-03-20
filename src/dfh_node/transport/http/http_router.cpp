/// \file http_router.cpp
/// \brief Реализация регистрации HTTP-маршрутов transport-слоя.
/// \details Содержит полный pipeline авторизации, постановки задач и формирования ответов.
///
#include "http_router.hpp"

#include "core/build_info.hpp"
#include "core/time_utils.hpp"
#include "core/version.hpp"
#include "http_error_map.hpp"
#include "http_reply_handle.hpp"
#include "security/sha256_utils.hpp"
#include "transport/http/http_dto_parser.hpp"
#include "transport/transport_security_utils.hpp"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace dfh_node::transport {
namespace {

using SwsResponse = SimpleWeb::Server<SimpleWeb::HTTP>::Response;
using SwsRequest = SimpleWeb::Server<SimpleWeb::HTTP>::Request;

enum class ContentLengthState { Absent, Valid, Invalid };

enum class HistoryResponseFormat { Csv, Dfhbin };

std::atomic<std::uint64_t> g_request_counter{0};

ContentLengthState read_content_length(const std::shared_ptr<SwsRequest> &request, std::int64_t &content_length_out) {
    content_length_out = 0;

    const auto it = request->header.find("Content-Length");
    if (it == request->header.end()) {
        return ContentLengthState::Absent;
    }

    const std::string raw_value = trim_copy(it->second);
    if (raw_value.empty()) {
        return ContentLengthState::Invalid;
    }

    char *end_ptr = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(raw_value.c_str(), &end_ptr, 10);
    if (errno != 0 || end_ptr == raw_value.c_str() || *end_ptr != '\0' || parsed < 0) {
        return ContentLengthState::Invalid;
    }

    content_length_out = static_cast<std::int64_t>(parsed);
    return ContentLengthState::Valid;
}

std::uint64_t steady_now_ms() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::string next_request_id() {
    const std::uint64_t id = g_request_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    return "http-" + std::to_string(id);
}

void send_response(const std::shared_ptr<SwsResponse> &response, const int status, const std::string &body,
                   const std::string &content_type, SimpleWeb::CaseInsensitiveMultimap extra_headers = {}) {
    extra_headers.emplace("Content-Type", content_type);
    response->write(static_cast<SimpleWeb::StatusCode>(status), body, extra_headers);
    response->send();
}

void send_error_code(const std::shared_ptr<SwsResponse> &response, const std::string_view error_code,
                     const std::string_view detail = "") {
    const auto [status, body] = error_code_to_http(error_code, detail);
    send_response(response, status, body, "application/json");
}

void send_gate_error(const std::shared_ptr<SwsResponse> &response, const GateError &error) {
    const auto [status, body] = gate_error_to_http(error);
    send_response(response, status, body, "application/json");
}

std::string adapter_status_to_string(const AdapterStatus status) {
    switch (status) {
    case AdapterStatus::Ok:
        return "ok";
    case AdapterStatus::Ignore:
        return "ignore";
    case AdapterStatus::Error:
        return "error";
    default:
        return "error";
    }
}

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

std::string encode_base64(const std::vector<std::uint8_t> &payload) {
    if (payload.empty()) {
        return {};
    }

    std::string encoded(((payload.size() + 2U) / 3U) * 4U, '\0');
    const int out_len = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(&encoded[0]), payload.data(),
                                        static_cast<int>(payload.size()));
    if (out_len <= 0) {
        return {};
    }
    encoded.resize(static_cast<std::size_t>(out_len));
    return encoded;
}

std::string escape_csv_field(const std::string &value) {
    bool needs_quotes = false;
    for (const char ch : value) {
        if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') {
            needs_quotes = true;
            break;
        }
    }

    if (!needs_quotes) {
        return value;
    }

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

std::string history_chunk_to_csv(const HistoryChunk &chunk) {
    std::ostringstream stream;
    stream << escape_csv_field(chunk.key.provider) << ',' << escape_csv_field(chunk.key.symbol) << ','
           << escape_csv_field(chunk.key.source) << ',' << timeframe_to_string(chunk.key.tf) << ','
           << chunk.key.block_ts << ',' << encode_base64(chunk.payload) << '\n';
    return stream.str();
}

bool append_with_limit(std::string &buffer, const std::string &chunk, const std::int64_t limit_bytes) {
    if (limit_bytes > 0) {
        const std::size_t limit = static_cast<std::size_t>(limit_bytes);
        if (buffer.size() + chunk.size() > limit) {
            return false;
        }
    }

    buffer.append(chunk);
    return true;
}

HistoryResponseFormat detect_history_format(const std::shared_ptr<SwsRequest> &request) {
    std::string query = request->query_string;
    if (!query.empty() && query.front() == '?') {
        query.erase(query.begin());
    }

    const auto params = SimpleWeb::QueryString::parse(query);
    const auto it = params.find("format");
    if (it != params.end() && it->second == "dfhbin") {
        return HistoryResponseFormat::Dfhbin;
    }

    return HistoryResponseFormat::Csv;
}

Task make_task(const TaskKind kind, std::string request_id, std::function<void()> payload) {
    Task task;
    task.kind = kind;
    task.request_id = std::move(request_id);
    task.enqueue_ts_ms = steady_now_ms();
    task.payload = std::move(payload);
    return task;
}

void reply_adapter_error(HttpReplyHandle &handle, const std::string &error_code, const std::string &detail) {
    if (error_code == "not_found") {
        handle.reply_error(404, "not_found", detail);
        return;
    }

    if (error_code == "invalid_argument") {
        handle.reply_error(400, "invalid_argument", detail);
        return;
    }

    handle.reply_error(500, "internal_error", detail);
}

void append_queue_metrics(nlohmann::json &output, const QueueMetrics &metrics) {
    output["current_size"] = metrics.current_size;
    output["capacity"] = metrics.capacity;
    output["rejected_count"] = metrics.rejected_count;
    output["dropped_count"] = metrics.dropped_count;
    output["total_enqueued"] = metrics.total_enqueued;
    output["total_processed"] = metrics.total_processed;
    output["avg_wait_ms"] = metrics.avg_wait_ms;
}

bool is_active_mdbx_key(const MdbxKeyRecord &record, const std::uint64_t now_ms) {
    if (record.revoked) {
        return false;
    }

    return !record.expires_at_ms.has_value() || *record.expires_at_ms > static_cast<std::int64_t>(now_ms);
}

} // namespace

HttpRouter::HttpRouter(UnifiedGate &gate, TaskScheduler &scheduler, IDfhAdapter &adapter, const config::Config &cfg,
                       DiskMonitor *disk_monitor, MdbxApiKeyStore *mdbx_store)
    : m_gate(gate), m_scheduler(scheduler), m_adapter(adapter), m_cfg(cfg), m_disk_monitor(disk_monitor),
      m_mdbx_store(mdbx_store), m_started_at(std::chrono::steady_clock::now()) {}

void HttpRouter::register_all(SimpleWeb::Server<SimpleWeb::HTTP> &server) {
    m_executor = server.io_service;

    server.resource["^/v1/ingest$"]["POST"] = [this](const std::shared_ptr<SwsResponse> &response,
                                                     const std::shared_ptr<SwsRequest> &request) {
        if (!m_executor) {
            send_error_code(response, "internal_error", "http executor is not initialized");
            return;
        }

        std::int64_t content_length = 0;
        switch (read_content_length(request, content_length)) {
        case ContentLengthState::Invalid:
            send_error_code(response, "invalid_query_param", "invalid Content-Length");
            return;
        case ContentLengthState::Valid:
            if (m_cfg.http.max_payload_bytes > 0 && content_length > m_cfg.http.max_payload_bytes) {
                send_error_code(response, "payload_too_large", "content-length exceeds max_payload_bytes");
                return;
            }
            break;
        case ContentLengthState::Absent:
            break;
        }

        const std::string body = request->content.string();
        if (m_cfg.http.max_payload_bytes > 0 && body.size() > static_cast<std::size_t>(m_cfg.http.max_payload_bytes)) {
            send_error_code(response, "payload_too_large", "body exceeds max_payload_bytes");
            return;
        }

        const std::string token = extract_bearer_token(request->header);
        HttpAntiReplayFields ar_fields;
        bool has_any_ar_header = false;
        const HttpAntiReplayFields *ar_ptr =
            parse_http_anti_replay_fields(request->method, request->path, request->query_string, request->header,
                                          compute_sha256_hex(body), ar_fields, has_any_ar_header);
        if (has_any_ar_header && ar_ptr == nullptr) {
            send_error_code(response, "missing_anti_replay_headers", "incomplete anti-replay headers");
            return;
        }

        const GateResult gate_result = m_gate.authorize_http(token, TaskKind::Ingest, ar_ptr);
        if (const auto *gate_error = std::get_if<GateError>(&gate_result)) {
            send_gate_error(response, *gate_error);
            return;
        }

        if (m_disk_monitor != nullptr && m_disk_monitor->is_disk_low()) {
            send_error_code(response, "disk_low", "Insufficient disk space");
            return;
        }

        auto parsed_ingest = parse_ingest_body(body, m_cfg.http);
        if (const auto *parse_error = std::get_if<ParseError>(&parsed_ingest)) {
            send_error_code(response, parse_error->first, parse_error->second);
            return;
        }

        auto blocks = std::get<std::vector<IngestRequest>>(std::move(parsed_ingest));
        const std::string request_id = next_request_id();

        HttpReplyHandle handle(response, m_executor, request_id);
        handle.start_timeout(std::chrono::milliseconds(m_cfg.http.request_timeout_ms));

        auto task = make_task(TaskKind::Ingest, request_id, [blocks = std::move(blocks), handle, this]() mutable {
            try {
                nlohmann::json payload;
                payload["results"] = nlohmann::json::array();

                for (auto &block : blocks) {
                    auto adapter_request = std::make_unique<IngestRequest>();
                    *adapter_request = std::move(block);
                    std::unique_ptr<IngestResponse> adapter_response =
                        m_adapter.ingest_structured(std::move(adapter_request));

                    nlohmann::json item;
                    if (!adapter_response) {
                        item["status"] = "error";
                        item["error_code"] = "internal";
                    } else {
                        item["status"] = adapter_status_to_string(adapter_response->status);
                        item["error_code"] = adapter_response->error_code;
                    }
                    payload["results"].push_back(std::move(item));
                }

                handle.reply_ok(payload.dump(), "application/json");
            } catch (const std::exception &e) {
                handle.reply_error(500, "internal_error", e.what());
            } catch (...) {
                handle.reply_error(500, "internal_error", "unknown ingest worker error");
            }
        });

        const EnqueueResult enqueue_result = m_scheduler.enqueue_high(std::move(task));
        if (enqueue_result.status != EnqueueStatus::Ok) {
            handle.reply_error(503, "queue_full", enqueue_result.error_message);
        }
    };

    server.resource["^/v1/history$"]["GET"] = [this](const std::shared_ptr<SwsResponse> &response,
                                                     const std::shared_ptr<SwsRequest> &request) {
        if (!m_executor) {
            send_error_code(response, "internal_error", "http executor is not initialized");
            return;
        }

        auto parsed_history = parse_history_query(request->query_string, m_cfg.http);
        if (const auto *parse_error = std::get_if<ParseError>(&parsed_history)) {
            send_error_code(response, parse_error->first, parse_error->second);
            return;
        }

        const HistoryResponseFormat format = detect_history_format(request);
        const std::string token = extract_bearer_token(request->header);

        HttpAntiReplayFields ar_fields;
        bool has_any_ar_header = false;
        const HttpAntiReplayFields *ar_ptr =
            parse_http_anti_replay_fields(request->method, request->path, request->query_string, request->header,
                                          compute_sha256_hex(""), ar_fields, has_any_ar_header);
        if (has_any_ar_header && ar_ptr == nullptr) {
            send_error_code(response, "missing_anti_replay_headers", "incomplete anti-replay headers");
            return;
        }

        const GateResult gate_result = m_gate.authorize_http(token, TaskKind::History, ar_ptr);
        if (const auto *gate_error = std::get_if<GateError>(&gate_result)) {
            send_gate_error(response, *gate_error);
            return;
        }

        auto history_dto = std::get<QueryHistoryRequest>(std::move(parsed_history));
        const std::string request_id = next_request_id();

        HttpReplyHandle handle(response, m_executor, request_id);
        handle.start_timeout(std::chrono::milliseconds(m_cfg.http.request_timeout_ms));

        auto task =
            make_task(TaskKind::History, request_id, [dto = std::move(history_dto), format, handle, this]() mutable {
                try {
                    auto adapter_request = std::make_unique<QueryHistoryRequest>();
                    *adapter_request = std::move(dto);
                    std::unique_ptr<QueryHistoryResponse> adapter_response =
                        m_adapter.query_history(std::move(adapter_request));
                    if (!adapter_response) {
                        handle.reply_error(500, "internal_error", "adapter returned null response");
                        return;
                    }

                    if (adapter_response->status == AdapterStatus::Error) {
                        reply_adapter_error(handle, adapter_response->error_code, adapter_response->error_code);
                        return;
                    }

                    std::string body;
                    if (format == HistoryResponseFormat::Csv) {
                        const std::string header_line = "provider,symbol,source,tf,block_ts,payload_base64\n";
                        if (!append_with_limit(body, header_line, m_cfg.http.history_max_bytes)) {
                            handle.reply_error(413, "response_too_large", "narrow the time range");
                            return;
                        }
                    }

                    for (const auto &chunk : adapter_response->chunks) {
                        std::string serialized_chunk;
                        if (format == HistoryResponseFormat::Csv) {
                            serialized_chunk = history_chunk_to_csv(chunk);
                        } else {
                            serialized_chunk.assign(reinterpret_cast<const char *>(chunk.payload.data()),
                                                    chunk.payload.size());
                        }

                        if (!append_with_limit(body, serialized_chunk, m_cfg.http.history_max_bytes)) {
                            handle.reply_error(413, "response_too_large", "narrow the time range");
                            return;
                        }
                    }

                    if (format == HistoryResponseFormat::Csv) {
                        SimpleWeb::CaseInsensitiveMultimap headers;
                        headers.emplace("Content-Disposition", "attachment; filename=\"history.csv\"");
                        handle.reply_ok(std::move(body), "text/csv", std::move(headers));
                        return;
                    }

                    SimpleWeb::CaseInsensitiveMultimap headers;
                    headers.emplace("Content-Disposition", "attachment; filename=\"history.dfhbin\"");
                    handle.reply_ok(std::move(body), "application/octet-stream", std::move(headers));
                } catch (const std::exception &e) {
                    handle.reply_error(500, "internal_error", e.what());
                } catch (...) {
                    handle.reply_error(500, "internal_error", "unknown history worker error");
                }
            });

        const EnqueueResult enqueue_result = m_scheduler.enqueue_low(std::move(task));
        if (enqueue_result.status != EnqueueStatus::Ok) {
            handle.reply_error(503, "queue_full", enqueue_result.error_message);
        }
    };

    server.resource["^/v1/status$"]["GET"] = [this](const std::shared_ptr<SwsResponse> &response,
                                                    const std::shared_ptr<SwsRequest> &request) {
        const std::string token = extract_bearer_token(request->header);

        const GateResult gate_result = m_gate.authorize_http(token, TaskKind::History, nullptr);
        if (const auto *gate_error = std::get_if<GateError>(&gate_result)) {
            send_gate_error(response, *gate_error);
            return;
        }

        const auto uptime_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_started_at)
                .count());

        QueueMetrics high_metrics = m_scheduler.high_metrics();
        QueueMetrics low_metrics = m_scheduler.low_metrics();

        nlohmann::json payload;
        payload["node_id"] = m_cfg.node_id;
        payload["version"] = std::string(version());
        payload["build_info"] = build_info_string();
        payload["uptime_ms"] = uptime_ms;
        payload["peers_count"] = m_cfg.peers.size();
        payload["env"] = m_cfg.env;
        payload["workers_count"] = m_cfg.queues.workers;

        nlohmann::json queues;
        append_queue_metrics(queues["high_priority_queue"], high_metrics);
        append_queue_metrics(queues["low_priority_queue"], low_metrics);
        payload["queues"] = std::move(queues);

        if (m_disk_monitor != nullptr) {
            payload["disk_free_bytes"] = m_disk_monitor->last_free_bytes();
            payload["disk_low"] = m_disk_monitor->is_disk_low();
        }

        if (m_mdbx_store != nullptr) {
            const auto all_keys = m_mdbx_store->list_all();
            const std::uint64_t now_ms = static_cast<std::uint64_t>(dfh_node::now_epoch_ms());
            std::uint64_t active_count = 0;
            for (const auto &record : all_keys) {
                if (is_active_mdbx_key(record, now_ms)) {
                    ++active_count;
                }
            }
            payload["mdbx_keys_active"] = active_count;
        }

        send_response(response, 200, payload.dump(), "application/json");
    };

    server.default_resource["GET"] = [](const std::shared_ptr<SwsResponse> &response,
                                        const std::shared_ptr<SwsRequest> &request) {
        (void)request;
        send_error_code(response, "not_found", "route not found");
    };

    server.default_resource["POST"] = [](const std::shared_ptr<SwsResponse> &response,
                                         const std::shared_ptr<SwsRequest> &request) {
        (void)request;
        send_error_code(response, "not_found", "route not found");
    };

    server.default_resource["PUT"] = [](const std::shared_ptr<SwsResponse> &response,
                                        const std::shared_ptr<SwsRequest> &request) {
        (void)request;
        send_error_code(response, "not_found", "route not found");
    };

    server.default_resource["DELETE"] = [](const std::shared_ptr<SwsResponse> &response,
                                           const std::shared_ptr<SwsRequest> &request) {
        (void)request;
        send_error_code(response, "not_found", "route not found");
    };

    server.on_error = [](const std::shared_ptr<SwsRequest> &request, const SimpleWeb::error_code &error_code) {
        if (!request) {
            std::clog << "WARN: HTTP transport error: " << error_code.message() << '\n';
            return;
        }

        std::clog << "WARN: HTTP transport error: method=" << request->method << ", path=" << request->path
                  << ", error=" << error_code.message() << '\n';
    };
}

} // namespace dfh_node::transport
