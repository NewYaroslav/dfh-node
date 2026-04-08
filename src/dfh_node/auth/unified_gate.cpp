/// \file unified_gate.cpp
/// \brief Реализация UnifiedGate для HTTP/WS авторизации.
/// \details Порядок: requirement -> auth -> rate-limit -> anti-replay (при наличии).
///
#include "unified_gate.hpp"

#include "core/logging.hpp"
#include "security/sha256_utils.hpp"

namespace dfh_node {
namespace {

const char *transport_to_string(const bool is_ws) { return is_ws ? "ws" : "http"; }

const char *kind_to_string(const TaskKind kind) {
    switch (kind) {
    case TaskKind::Ingest:
        return "ingest";
    case TaskKind::History:
        return "history";
    default:
        return "unknown";
    }
}

const char *gate_code_to_string(const GateErrorCode code) {
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

    return "unknown";
}

void log_gate_reject(const GateErrorCode code, const bool is_ws, const TaskKind kind, const std::string &fingerprint) {
    logging::log_gate_reject(gate_code_to_string(code), transport_to_string(is_ws), kind_to_string(kind),
                             fingerprint.empty() ? "unknown" : fingerprint.c_str());
}

} // namespace

UnifiedGate::UnifiedGate(AuthService &auth_service, RateLimiter &rate_limiter, WsConnectionLimiter &ws_limiter,
                         AntiReplayValidator *anti_replay_validator, const ScopeMask require_for_scopes)
    : m_auth_service(auth_service), m_rate_limiter(rate_limiter), m_ws_limiter(ws_limiter),
      m_anti_replay_validator(anti_replay_validator), m_require_for_scopes(require_for_scopes) {}

GateResult UnifiedGate::authorize_http(const std::string &token, const TaskKind kind,
                                       const HttpAntiReplayFields *ar_fields) {
    const auto scope_opt = required_scope(kind);
    if (!scope_opt.has_value()) {
        return GateError{GateErrorCode::UnsupportedOperation, "Unknown TaskKind"};
    }
    const bool anti_replay_required = is_anti_replay_required(kind);

    const GateResult auth_result = m_auth_service.authorize(token, kind);
    if (const auto *err = std::get_if<GateError>(&auth_result)) {
        if (err->code == GateErrorCode::Unauthorized || err->code == GateErrorCode::Forbidden) {
            m_auth_fail_count.fetch_add(1, std::memory_order_relaxed);
        }
        log_gate_reject(err->code, false, kind, "");
        return *err;
    }

    const auto *ctx = std::get_if<AuthContext>(&auth_result);
    if (ctx == nullptr) {
        m_auth_fail_count.fetch_add(1, std::memory_order_relaxed);
        log_gate_reject(GateErrorCode::Unauthorized, false, kind, "");
        return GateError{GateErrorCode::Unauthorized, "Authorization result does not contain context"};
    }

    if (!m_rate_limiter.check_and_record(ctx->fingerprint, ctx->rps_limit)) {
        m_rate_limit_reject_count.fetch_add(1, std::memory_order_relaxed);
        log_gate_reject(GateErrorCode::RateLimited, false, kind, ctx->fingerprint);
        return GateError{GateErrorCode::RateLimited, "Rate limit exceeded"};
    }

    if (m_anti_replay_validator == nullptr) {
        if (anti_replay_required) {
            m_anti_replay_reject_count.fetch_add(1, std::memory_order_relaxed);
            log_gate_reject(GateErrorCode::AntiReplayRequired, false, kind, ctx->fingerprint);
            return GateError{GateErrorCode::AntiReplayRequired, "Anti-replay is disabled but required for operation"};
        }
        return *ctx;
    }

    if (ar_fields == nullptr) {
        if (anti_replay_required) {
            m_anti_replay_reject_count.fetch_add(1, std::memory_order_relaxed);
            log_gate_reject(GateErrorCode::MissingAntiReplayHeaders, false, kind, ctx->fingerprint);
            return GateError{GateErrorCode::MissingAntiReplayHeaders, "Anti-replay headers missing"};
        }
        return *ctx;
    }

    unsigned char signing_key[32];
    compute_sha256_raw(token, signing_key);

    HttpCanonicalInput input;
    input.method = ar_fields->method;
    input.path = ar_fields->path;
    input.query_params = ar_fields->query_params;
    input.timestamp = ar_fields->timestamp;
    input.nonce = ar_fields->nonce;
    input.body_hash = ar_fields->body_hash;

    const GateResult ar_result =
        m_anti_replay_validator->validate_http(ctx->fingerprint, signing_key, 32u, input, ar_fields->signature);
    if (const auto *err = std::get_if<GateError>(&ar_result)) {
        if (err->code == GateErrorCode::AntiReplayFailed || err->code == GateErrorCode::AntiReplayRequired ||
            err->code == GateErrorCode::MissingAntiReplayHeaders ||
            err->code == GateErrorCode::MissingAntiReplayFields) {
            m_anti_replay_reject_count.fetch_add(1, std::memory_order_relaxed);
        }
        log_gate_reject(err->code, false, kind, ctx->fingerprint);
        return *err;
    }

    return *ctx;
}

GateResult UnifiedGate::authorize_ws_upgrade(const std::string &token) {
    const GateResult auth_result = m_auth_service.authenticate_token(token);
    if (const auto *err = std::get_if<GateError>(&auth_result)) {
        if (err->code == GateErrorCode::Unauthorized || err->code == GateErrorCode::Forbidden) {
            m_auth_fail_count.fetch_add(1, std::memory_order_relaxed);
        }
        log_gate_reject(err->code, true, TaskKind::Ingest, "");
        return *err;
    }

    const auto *ctx = std::get_if<AuthContext>(&auth_result);
    if (ctx == nullptr) {
        m_auth_fail_count.fetch_add(1, std::memory_order_relaxed);
        log_gate_reject(GateErrorCode::Unauthorized, true, TaskKind::Ingest, "");
        return GateError{GateErrorCode::Unauthorized, "Authentication result does not contain context"};
    }

    if (!m_rate_limiter.check_and_record(ctx->fingerprint, ctx->rps_limit)) {
        m_rate_limit_reject_count.fetch_add(1, std::memory_order_relaxed);
        log_gate_reject(GateErrorCode::RateLimited, true, TaskKind::Ingest, ctx->fingerprint);
        return GateError{GateErrorCode::RateLimited, "Rate limit exceeded"};
    }

    if (!m_ws_limiter.register_connection(ctx->fingerprint, ctx->ws_max_connections)) {
        m_connection_limit_reject_count.fetch_add(1, std::memory_order_relaxed);
        log_gate_reject(GateErrorCode::ConnectionLimited, true, TaskKind::Ingest, ctx->fingerprint);
        return GateError{GateErrorCode::ConnectionLimited, "WebSocket connection limit exceeded"};
    }

    return *ctx;
}

GateResult UnifiedGate::authorize_ws_message(const std::string &fingerprint, const TaskKind kind,
                                             const unsigned char *signing_key, const std::size_t key_len,
                                             const WsAntiReplayFields *ar_fields) {
    const auto scope_opt = required_scope(kind);
    if (!scope_opt.has_value()) {
        return GateError{GateErrorCode::UnsupportedOperation, "Unknown TaskKind"};
    }
    const bool anti_replay_required = is_anti_replay_required(kind);

    const GateResult auth_result = m_auth_service.authorize_fingerprint(fingerprint, kind);
    if (const auto *err = std::get_if<GateError>(&auth_result)) {
        if (err->code == GateErrorCode::Unauthorized || err->code == GateErrorCode::Forbidden) {
            m_auth_fail_count.fetch_add(1, std::memory_order_relaxed);
        }
        log_gate_reject(err->code, true, kind, fingerprint);
        return *err;
    }

    const auto *ctx = std::get_if<AuthContext>(&auth_result);
    if (ctx == nullptr) {
        m_auth_fail_count.fetch_add(1, std::memory_order_relaxed);
        log_gate_reject(GateErrorCode::Unauthorized, true, kind, fingerprint);
        return GateError{GateErrorCode::Unauthorized, "Authorization result does not contain context"};
    }

    if (!m_rate_limiter.check_and_record(ctx->fingerprint, ctx->rps_limit)) {
        m_rate_limit_reject_count.fetch_add(1, std::memory_order_relaxed);
        log_gate_reject(GateErrorCode::RateLimited, true, kind, ctx->fingerprint);
        return GateError{GateErrorCode::RateLimited, "Rate limit exceeded"};
    }

    if (m_anti_replay_validator == nullptr) {
        if (anti_replay_required) {
            m_anti_replay_reject_count.fetch_add(1, std::memory_order_relaxed);
            log_gate_reject(GateErrorCode::AntiReplayRequired, true, kind, ctx->fingerprint);
            return GateError{GateErrorCode::AntiReplayRequired, "Anti-replay is disabled but required for operation"};
        }
        return *ctx;
    }

    if (ar_fields == nullptr) {
        if (anti_replay_required) {
            m_anti_replay_reject_count.fetch_add(1, std::memory_order_relaxed);
            log_gate_reject(GateErrorCode::MissingAntiReplayFields, true, kind, ctx->fingerprint);
            return GateError{GateErrorCode::MissingAntiReplayFields, "Anti-replay fields missing in WS message"};
        }
        return *ctx;
    }

    WsCanonicalInput input;
    input.endpoint = ar_fields->endpoint;
    input.op = ar_fields->op;
    input.msg_id = ar_fields->msg_id;
    input.timestamp = ar_fields->timestamp;
    input.nonce = ar_fields->nonce;
    input.payload_hash = ar_fields->payload_hash;

    const GateResult ar_result =
        m_anti_replay_validator->validate_ws(ctx->fingerprint, signing_key, key_len, input, ar_fields->signature);
    if (const auto *err = std::get_if<GateError>(&ar_result)) {
        if (err->code == GateErrorCode::AntiReplayFailed || err->code == GateErrorCode::AntiReplayRequired ||
            err->code == GateErrorCode::MissingAntiReplayHeaders ||
            err->code == GateErrorCode::MissingAntiReplayFields) {
            m_anti_replay_reject_count.fetch_add(1, std::memory_order_relaxed);
        }
        log_gate_reject(err->code, true, kind, ctx->fingerprint);
        return *err;
    }

    return *ctx;
}

void UnifiedGate::ws_connection_closed(const std::string &fingerprint) {
    m_ws_limiter.unregister_connection(fingerprint);
}

std::uint64_t UnifiedGate::auth_fail_count() const { return m_auth_fail_count.load(std::memory_order_relaxed); }

std::uint64_t UnifiedGate::rate_limit_reject_count() const {
    return m_rate_limit_reject_count.load(std::memory_order_relaxed);
}

std::uint64_t UnifiedGate::anti_replay_reject_count() const {
    return m_anti_replay_reject_count.load(std::memory_order_relaxed);
}

std::uint64_t UnifiedGate::connection_limit_reject_count() const {
    return m_connection_limit_reject_count.load(std::memory_order_relaxed);
}

std::int64_t UnifiedGate::ws_active_connections() const { return m_ws_limiter.total_active_connections(); }

bool UnifiedGate::is_anti_replay_required(const TaskKind kind) const {
    const auto scope_opt = required_scope(kind);
    if (!scope_opt.has_value()) {
        return false;
    }

    const Scope scope = *scope_opt;
    // Для require_for_scopes проверяем только прямое вхождение бита,
    // без admin-override из has_scope().
    return (m_require_for_scopes & to_scope_mask(scope)) != 0;
}

} // namespace dfh_node
