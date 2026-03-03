/// \file ws_message_handler.cpp
/// \brief Реализация конвейера обработки WS control/binary сообщений.
/// \details Выполняет разбор сообщений, `gate`-проверки, постановку задач в очереди и
/// отправку ответов клиенту в форматах `json`/`msgpack`.
///
#include "ws_message_handler.hpp"

#include "security/sha256_utils.hpp"
#include "ws_dto_parser.hpp"

#include <LogIt.hpp>
#include <openssl/evp.h>

#include "core/logging.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace dfh_node::transport {
namespace {

std::uint64_t steady_now_ms() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

Task make_task(const TaskKind kind, std::string request_id, std::function<void()> payload) {
    Task task;
    task.kind = kind;
    task.request_id = std::move(request_id);
    task.enqueue_ts_ms = steady_now_ms();
    task.payload = std::move(payload);
    return task;
}

std::string ws_op_to_string(const WsOp op) {
    switch (op) {
    case WsOp::Ingest:
        return "ingest";
    case WsOp::History:
        return "history";
    case WsOp::Subscribe:
        return "subscribe";
    }

    return "history";
}

WsResponseMessage make_error_response(std::string msg_id, std::string error_code, std::string detail) {
    WsResponseMessage response;
    response.msg_id = std::move(msg_id);
    response.ok = false;
    response.error_code = std::move(error_code);
    response.detail = std::move(detail);
    return response;
}

WsResponseMessage make_ok_response(std::string msg_id, nlohmann::json data) {
    WsResponseMessage response;
    response.msg_id = std::move(msg_id);
    response.ok = true;
    response.data = std::move(data);
    return response;
}

std::string map_gate_error_code(const GateErrorCode code) {
    switch (code) {
    case GateErrorCode::Unauthorized:
        return "unauthorized";
    case GateErrorCode::Forbidden:
        return "forbidden";
    case GateErrorCode::RateLimited:
        return "rate_limited";
    case GateErrorCode::ConnectionLimited:
        return "connection_limited";
    case GateErrorCode::UnsupportedOperation:
        return "unsupported_operation";
    case GateErrorCode::AntiReplayFailed:
        return "anti_replay_failed";
    case GateErrorCode::AntiReplayRequired:
        return "anti_replay_required";
    case GateErrorCode::MissingAntiReplayHeaders:
        return "missing_anti_replay_headers";
    case GateErrorCode::MissingAntiReplayFields:
        return "missing_anti_replay_fields";
    }

    return "internal_error";
}

std::string map_adapter_error_code(const std::string &error_code) {
    if (error_code == "invalid_argument") {
        return "invalid_argument";
    }
    if (error_code == "not_found") {
        return "not_found";
    }
    if (error_code.empty() || error_code == "internal") {
        return "internal_error";
    }
    return error_code;
}

std::string adapter_status_to_string(const AdapterStatus status) {
    switch (status) {
    case AdapterStatus::Ok:
        return "ok";
    case AdapterStatus::Ignore:
        return "ignore";
    case AdapterStatus::Error:
        return "error";
    }

    return "error";
}

std::string timeframe_to_string(const Timeframe tf) {
    switch (tf) {
    case Timeframe::Ticks:
        return "ticks";
    case Timeframe::M1:
        return "m1";
    }

    return "unknown";
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

nlohmann::json block_key_to_json(const BlockKey &key) {
    return {
        {"provider", key.provider},          {"symbol", key.symbol},     {"source", key.source},
        {"tf", timeframe_to_string(key.tf)}, {"block_ts", key.block_ts},
    };
}

void send_response_via_registry(const std::shared_ptr<WsSessionRegistry> &registry, const std::string &conn_id,
                                const WsResponseMessage &resp, const bool is_msgpack) {
    if (!registry) {
        return;
    }

    const auto conn_weak = registry->get_connection(conn_id);
    auto conn = conn_weak.lock();
    if (!conn) {
        DFH_PRINTF_WARN("WS response dropped: connection is not available, id=%s", conn_id.c_str());
        return;
    }

    try {
        if (is_msgpack) {
            const auto bytes = serialize_ws_msgpack_response(resp);
            std::string payload;
            if (!bytes.empty()) {
                payload.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            }
            // Потокобезопасность `send()` подтверждена по
            // `simple-websocket-server` commit `89e5677789d096374edb93aaabaf23799a7e1692`:
            // `server_ws.hpp` строки 160-165 (`send_queue_mutex` + `send_from_queue`) и
            // строки 230-232 (`send_queue.emplace_back(...)` под тем же `LockGuard`).
            conn->send(payload, nullptr, 130);
            return;
        }

        conn->send(serialize_ws_json_response(resp));
    } catch (const std::exception &e) {
        DFH_PRINTF_WARN("WS send failed: id=%s, error=%s", conn_id.c_str(), e.what());
    } catch (...) {
        DFH_PRINTF_WARN("WS send failed: id=%s, error=unknown", conn_id.c_str());
    }
}

} // namespace

WsMessageHandler::WsMessageHandler(UnifiedGate &gate, TaskScheduler &scheduler, IDfhAdapter &adapter,
                                   const config::Config &cfg, std::shared_ptr<WsSessionRegistry> registry)
    : m_gate(gate), m_scheduler(scheduler), m_adapter(adapter), m_cfg(cfg), m_registry(std::move(registry)) {
    if (!m_registry) {
        throw std::invalid_argument("WsMessageHandler requires non-null WsSessionRegistry");
    }
}

void WsMessageHandler::handle_text(const std::string &connection_id, const std::string &message, bool is_msgpack) {
    ParseResult<WsControlMessage> parsed = parse_ws_json(message);
    if (is_msgpack) {
        const std::vector<std::uint8_t> bytes(message.begin(), message.end());
        parsed = parse_ws_msgpack(bytes);
    }

    if (const auto *parse_error = std::get_if<ParseError>(&parsed)) {
        send_response(connection_id, make_error_response("", parse_error->first, parse_error->second), is_msgpack);
        return;
    }

    WsControlMessage msg = std::get<WsControlMessage>(std::move(parsed));
    const std::optional<WsConnectionContext> ctx_opt = m_registry->get_context(connection_id);
    if (!ctx_opt.has_value()) {
        DFH_PRINTF_WARN("WS control dropped: unknown connection id=%s", connection_id.c_str());
        return;
    }

    const WsConnectionContext &ctx = *ctx_opt;
    WsAntiReplayFields anti_replay_fields;
    anti_replay_fields.endpoint = ctx.endpoint;
    anti_replay_fields.op = ws_op_to_string(msg.op);
    anti_replay_fields.msg_id = msg.msg_id;
    anti_replay_fields.timestamp = msg.timestamp;
    anti_replay_fields.nonce = msg.nonce;
    anti_replay_fields.signature = msg.signature;
    anti_replay_fields.payload_hash = msg.payload_hash;

    const TaskKind task_kind = ws_op_to_task_kind(msg.op);
    const GateResult gate_result = m_gate.authorize_ws_message(ctx.fingerprint, task_kind, ctx.signing_key,
                                                               sizeof(ctx.signing_key), &anti_replay_fields);
    if (const auto *gate_error = std::get_if<GateError>(&gate_result)) {
        send_response(connection_id,
                      make_error_response(msg.msg_id, map_gate_error_code(gate_error->code), gate_error->message),
                      is_msgpack);
        return;
    }

    switch (msg.op) {
    case WsOp::Ingest:
        if (msg.payload.contains("payload_base64")) {
            handle_ingest(connection_id, msg, is_msgpack);
            return;
        }
        handle_dfhbin_control(connection_id, msg);
        return;

    case WsOp::History:
        handle_history(connection_id, msg, is_msgpack);
        return;

    case WsOp::Subscribe:
        handle_subscribe(connection_id, msg.msg_id, is_msgpack);
        return;
    }
}

void WsMessageHandler::handle_binary(const std::string &connection_id, const std::vector<std::uint8_t> &bytes) {
    const bool is_msgpack = is_msgpack_connection(connection_id);
    if (m_cfg.ws.max_payload_bytes > 0 && bytes.size() > static_cast<std::size_t>(m_cfg.ws.max_payload_bytes)) {
        send_response(connection_id,
                      make_error_response("", "payload_too_large", "binary frame exceeds max_payload_bytes"),
                      is_msgpack);
        return;
    }

    std::optional<PendingDfhbinState> pending_opt = m_registry->take_pending_dfhbin(connection_id);
    if (!pending_opt.has_value()) {
        send_response(connection_id,
                      make_error_response("", "unexpected_binary_frame", "missing pending dfhbin control-message"),
                      is_msgpack);
        return;
    }

    PendingDfhbinState pending = std::move(*pending_opt);
    std::string payload;
    if (!bytes.empty()) {
        payload.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }
    const std::string computed_sha256 = compute_sha256_hex(payload);
    if (computed_sha256 != pending.payload_sha256) {
        send_response(connection_id,
                      make_error_response(pending.msg_id, "sha256_mismatch", "binary frame hash does not match"),
                      is_msgpack);
        return;
    }

    auto dto_holder = std::make_shared<std::unique_ptr<IngestRequest>>(std::make_unique<IngestRequest>());
    (*dto_holder)->key = std::move(pending.block_key);
    (*dto_holder)->payload.assign(bytes.begin(), bytes.end());

    const std::string msg_id = pending.msg_id;
    auto registry = m_registry;
    auto task = make_task(
        TaskKind::Ingest, msg_id,
        [dto_holder, registry, conn_id = connection_id, msg_id, is_msgpack, &adapter = m_adapter]() mutable {
            try {
                std::unique_ptr<IngestResponse> adapter_response = adapter.ingest_structured(std::move(*dto_holder));
                if (!registry->get_context(conn_id).has_value()) {
                    DFH_PRINTF_WARN("WS binary ingest reply skipped: connection closed, id=%s", conn_id.c_str());
                    return;
                }

                if (!adapter_response) {
                    send_response_via_registry(
                        registry, conn_id,
                        make_error_response(msg_id, "internal_error", "adapter returned null response"), is_msgpack);
                    return;
                }

                if (adapter_response->status == AdapterStatus::Error) {
                    const std::string detail =
                        adapter_response->error_code.empty() ? "adapter error" : adapter_response->error_code;
                    send_response_via_registry(
                        registry, conn_id,
                        make_error_response(msg_id, map_adapter_error_code(adapter_response->error_code), detail),
                        is_msgpack);
                    return;
                }

                nlohmann::json data;
                data["status"] = adapter_status_to_string(adapter_response->status);
                if (!adapter_response->error_code.empty()) {
                    data["error_code"] = adapter_response->error_code;
                }
                send_response_via_registry(registry, conn_id, make_ok_response(msg_id, std::move(data)), is_msgpack);
            } catch (const std::exception &e) {
                send_response_via_registry(registry, conn_id, make_error_response(msg_id, "internal_error", e.what()),
                                           is_msgpack);
            } catch (...) {
                send_response_via_registry(
                    registry, conn_id,
                    make_error_response(msg_id, "internal_error", "unknown binary ingest worker error"), is_msgpack);
            }
        });

    const EnqueueResult enqueue_result = m_scheduler.enqueue_high(std::move(task));
    if (enqueue_result.status == EnqueueStatus::Rejected || enqueue_result.status == EnqueueStatus::Dropped) {
        const std::string error_code =
            enqueue_result.error_code.empty() ? "overload.high_priority_queue_full" : enqueue_result.error_code;
        handle_overload(connection_id, msg_id, error_code, is_msgpack);
    }
}

void WsMessageHandler::handle_ingest(const std::string &conn_id, const WsControlMessage &msg, const bool is_msgpack) {
    ParseResult<IngestRequest> parsed = parse_ws_ingest_payload(msg.payload, m_cfg.ws);
    if (const auto *parse_error = std::get_if<ParseError>(&parsed)) {
        send_response(conn_id, make_error_response(msg.msg_id, parse_error->first, parse_error->second), is_msgpack);
        return;
    }

    auto dto_holder = std::make_shared<std::unique_ptr<IngestRequest>>(std::make_unique<IngestRequest>());
    *(*dto_holder) = std::get<IngestRequest>(std::move(parsed));

    const std::string msg_id = msg.msg_id;
    auto registry = m_registry;
    auto task = make_task(
        TaskKind::Ingest, msg_id, [dto_holder, registry, conn_id, msg_id, is_msgpack, &adapter = m_adapter]() mutable {
            try {
                std::unique_ptr<IngestResponse> adapter_response = adapter.ingest_structured(std::move(*dto_holder));
                if (!registry->get_context(conn_id).has_value()) {
                    DFH_PRINTF_WARN("WS ingest reply skipped: connection closed, id=%s", conn_id.c_str());
                    return;
                }

                if (!adapter_response) {
                    send_response_via_registry(
                        registry, conn_id,
                        make_error_response(msg_id, "internal_error", "adapter returned null response"), is_msgpack);
                    return;
                }

                if (adapter_response->status == AdapterStatus::Error) {
                    const std::string detail =
                        adapter_response->error_code.empty() ? "adapter error" : adapter_response->error_code;
                    send_response_via_registry(
                        registry, conn_id,
                        make_error_response(msg_id, map_adapter_error_code(adapter_response->error_code), detail),
                        is_msgpack);
                    return;
                }

                nlohmann::json data;
                data["status"] = adapter_status_to_string(adapter_response->status);
                if (!adapter_response->error_code.empty()) {
                    data["error_code"] = adapter_response->error_code;
                }
                send_response_via_registry(registry, conn_id, make_ok_response(msg_id, std::move(data)), is_msgpack);
            } catch (const std::exception &e) {
                send_response_via_registry(registry, conn_id, make_error_response(msg_id, "internal_error", e.what()),
                                           is_msgpack);
            } catch (...) {
                send_response_via_registry(registry, conn_id,
                                           make_error_response(msg_id, "internal_error", "unknown ingest worker error"),
                                           is_msgpack);
            }
        });

    const EnqueueResult enqueue_result = m_scheduler.enqueue_high(std::move(task));
    if (enqueue_result.status == EnqueueStatus::Rejected || enqueue_result.status == EnqueueStatus::Dropped) {
        const std::string error_code =
            enqueue_result.error_code.empty() ? "overload.high_priority_queue_full" : enqueue_result.error_code;
        handle_overload(conn_id, msg_id, error_code, is_msgpack);
    }
}

void WsMessageHandler::handle_history(const std::string &conn_id, const WsControlMessage &msg, const bool is_msgpack) {
    ParseResult<QueryHistoryRequest> parsed = parse_ws_history_payload(msg.payload, m_cfg.ws);
    if (const auto *parse_error = std::get_if<ParseError>(&parsed)) {
        send_response(conn_id, make_error_response(msg.msg_id, parse_error->first, parse_error->second), is_msgpack);
        return;
    }

    auto dto_holder = std::make_shared<std::unique_ptr<QueryHistoryRequest>>(std::make_unique<QueryHistoryRequest>());
    *(*dto_holder) = std::get<QueryHistoryRequest>(std::move(parsed));

    const std::string msg_id = msg.msg_id;
    auto registry = m_registry;
    auto task = make_task(
        TaskKind::History, msg_id, [dto_holder, registry, conn_id, msg_id, is_msgpack, &adapter = m_adapter]() mutable {
            try {
                std::unique_ptr<QueryHistoryResponse> adapter_response = adapter.query_history(std::move(*dto_holder));
                if (!registry->get_context(conn_id).has_value()) {
                    DFH_PRINTF_WARN("WS history reply skipped: connection closed, id=%s", conn_id.c_str());
                    return;
                }

                if (!adapter_response) {
                    send_response_via_registry(
                        registry, conn_id,
                        make_error_response(msg_id, "internal_error", "adapter returned null response"), is_msgpack);
                    return;
                }

                if (adapter_response->status == AdapterStatus::Error) {
                    const std::string detail =
                        adapter_response->error_code.empty() ? "adapter error" : adapter_response->error_code;
                    send_response_via_registry(
                        registry, conn_id,
                        make_error_response(msg_id, map_adapter_error_code(adapter_response->error_code), detail),
                        is_msgpack);
                    return;
                }

                nlohmann::json data;
                data["chunks"] = nlohmann::json::array();
                for (const auto &chunk : adapter_response->chunks) {
                    nlohmann::json item;
                    item["key"] = block_key_to_json(chunk.key);
                    item["payload_base64"] = encode_base64(chunk.payload);
                    data["chunks"].push_back(std::move(item));
                }

                send_response_via_registry(registry, conn_id, make_ok_response(msg_id, std::move(data)), is_msgpack);
            } catch (const std::exception &e) {
                send_response_via_registry(registry, conn_id, make_error_response(msg_id, "internal_error", e.what()),
                                           is_msgpack);
            } catch (...) {
                send_response_via_registry(
                    registry, conn_id, make_error_response(msg_id, "internal_error", "unknown history worker error"),
                    is_msgpack);
            }
        });

    const EnqueueResult enqueue_result = m_scheduler.enqueue_low(std::move(task));
    if (enqueue_result.status == EnqueueStatus::Rejected || enqueue_result.status == EnqueueStatus::Dropped) {
        const std::string error_code =
            enqueue_result.error_code.empty() ? "overload.low_priority_queue_full" : enqueue_result.error_code;
        handle_overload(conn_id, msg_id, error_code, is_msgpack);
    }
}

void WsMessageHandler::handle_dfhbin_control(const std::string &conn_id, const WsControlMessage &msg) {
    const bool is_msgpack = is_msgpack_connection(conn_id);
    if (msg.payload_sha256.empty()) {
        send_response(conn_id, make_error_response(msg.msg_id, "invalid_argument", "missing field: payload_sha256"),
                      is_msgpack);
        return;
    }

    ParseResult<BlockKey> parsed = parse_ws_dfhbin_payload(msg.payload);
    if (const auto *parse_error = std::get_if<ParseError>(&parsed)) {
        send_response(conn_id, make_error_response(msg.msg_id, parse_error->first, parse_error->second), is_msgpack);
        return;
    }

    PendingDfhbinState state;
    state.msg_id = msg.msg_id;
    state.payload_sha256 = msg.payload_sha256;
    state.block_key = std::get<BlockKey>(std::move(parsed));
    m_registry->set_pending_dfhbin(conn_id, std::move(state));
}

void WsMessageHandler::handle_subscribe(const std::string &conn_id, const std::string &msg_id, const bool is_msgpack) {
    send_response(conn_id, make_error_response(msg_id, "unsupported_operation", "op=subscribe is not implemented"),
                  is_msgpack);
}

void WsMessageHandler::send_response(const std::string &conn_id, const WsResponseMessage &resp, const bool is_msgpack) {
    send_response_via_registry(m_registry, conn_id, resp, is_msgpack);
}

void WsMessageHandler::handle_overload(const std::string &conn_id, const std::string &msg_id,
                                       const std::string &error_code, const bool is_msgpack) {
    const std::string normalized_code = error_code.empty() ? "overload.high_priority_queue_full" : error_code;
    send_response(conn_id, make_error_response(msg_id, normalized_code, "queue is full"), is_msgpack);
}

bool WsMessageHandler::is_msgpack_connection(const std::string &conn_id) const {
    const auto ctx_opt = m_registry->get_context(conn_id);
    if (!ctx_opt.has_value()) {
        return false;
    }
    return ctx_opt->endpoint == "/ws/msgpack";
}

} // namespace dfh_node::transport
