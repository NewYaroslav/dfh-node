/// \file unified_gate.cpp
/// \brief Реализация UnifiedGate для HTTP/WS авторизации.
/// \details Порядок: requirement -> auth -> rate-limit -> anti-replay (при наличии).
///
#include "unified_gate.hpp"

#include "sha256_utils.hpp"

namespace dfh_node {

UnifiedGate::UnifiedGate(AuthService &auth_service, RateLimiter &rate_limiter, WsConnectionLimiter &ws_limiter,
                         AntiReplayValidator *anti_replay_validator, const ScopeMask require_for_scopes)
    : m_auth_service(auth_service), m_rate_limiter(rate_limiter), m_ws_limiter(ws_limiter),
      m_anti_replay_validator(anti_replay_validator), m_require_for_scopes(require_for_scopes) {}

GateResult UnifiedGate::authorize_http(const std::string &token, const TaskKind kind,
                                       const HttpAntiReplayFields *ar_fields) {
    const GateResult requirement_check = validate_anti_replay_requirement(kind);
    if (const auto *err = std::get_if<GateError>(&requirement_check)) {
        return *err;
    }

    const GateResult auth_result = m_auth_service.authorize(token, kind);
    if (const auto *err = std::get_if<GateError>(&auth_result)) {
        return *err;
    }

    const auto *ctx = std::get_if<AuthContext>(&auth_result);
    if (ctx == nullptr) {
        return GateError{GateErrorCode::Unauthorized, "Authorization result does not contain context"};
    }

    if (!m_rate_limiter.check_and_record(ctx->fingerprint, ctx->rps_limit)) {
        return GateError{GateErrorCode::RateLimited, "Rate limit exceeded"};
    }

    if (m_anti_replay_validator != nullptr) {
        if (ar_fields == nullptr) {
            return GateError{GateErrorCode::MissingAntiReplayHeaders, "Anti-replay headers missing"};
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
            return *err;
        }
    }

    return *ctx;
}

GateResult UnifiedGate::authorize_ws_upgrade(const std::string &token) {
    const GateResult auth_result = m_auth_service.authenticate_token(token);
    if (const auto *err = std::get_if<GateError>(&auth_result)) {
        return *err;
    }

    const auto *ctx = std::get_if<AuthContext>(&auth_result);
    if (ctx == nullptr) {
        return GateError{GateErrorCode::Unauthorized, "Authentication result does not contain context"};
    }

    if (!m_rate_limiter.check_and_record(ctx->fingerprint, ctx->rps_limit)) {
        return GateError{GateErrorCode::RateLimited, "Rate limit exceeded"};
    }

    if (!m_ws_limiter.register_connection(ctx->fingerprint, ctx->ws_max_connections)) {
        return GateError{GateErrorCode::ConnectionLimited, "WebSocket connection limit exceeded"};
    }

    return *ctx;
}

GateResult UnifiedGate::authorize_ws_message(const std::string &fingerprint, const TaskKind kind,
                                             const unsigned char *signing_key, const std::size_t key_len,
                                             const WsAntiReplayFields *ar_fields) {
    const GateResult requirement_check = validate_anti_replay_requirement(kind);
    if (const auto *err = std::get_if<GateError>(&requirement_check)) {
        return *err;
    }

    const GateResult auth_result = m_auth_service.authorize_fingerprint(fingerprint, kind);
    if (const auto *err = std::get_if<GateError>(&auth_result)) {
        return *err;
    }

    const auto *ctx = std::get_if<AuthContext>(&auth_result);
    if (ctx == nullptr) {
        return GateError{GateErrorCode::Unauthorized, "Authorization result does not contain context"};
    }

    if (!m_rate_limiter.check_and_record(ctx->fingerprint, ctx->rps_limit)) {
        return GateError{GateErrorCode::RateLimited, "Rate limit exceeded"};
    }

    if (m_anti_replay_validator != nullptr) {
        if (ar_fields == nullptr) {
            return GateError{GateErrorCode::MissingAntiReplayFields, "Anti-replay fields missing in WS message"};
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
            return *err;
        }
    }

    return *ctx;
}

void UnifiedGate::ws_connection_closed(const std::string &fingerprint) {
    m_ws_limiter.unregister_connection(fingerprint);
}

GateResult UnifiedGate::validate_anti_replay_requirement(const TaskKind kind) const {
    const auto scope_opt = required_scope(kind);
    if (!scope_opt.has_value()) {
        return GateError{GateErrorCode::UnsupportedOperation, "Unknown TaskKind"};
    }

    if (m_anti_replay_validator != nullptr) {
        return std::monostate{};
    }

    const Scope scope = *scope_opt;
    // Для require_for_scopes проверяем только прямое вхождение бита,
    // без admin-override из has_scope().
    if ((m_require_for_scopes & to_scope_mask(scope)) != 0) {
        return GateError{GateErrorCode::AntiReplayRequired, "Anti-replay is disabled but required for operation"};
    }

    return std::monostate{};
}

} // namespace dfh_node
