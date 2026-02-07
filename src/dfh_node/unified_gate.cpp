/**
 * \file unified_gate.cpp
 * \brief Реализация единого gate для HTTP/WS авторизации.
 * \details Проверяет auth/scope, применяет rate limit и лимит WS-соединений.
 */
#include "unified_gate.hpp"

#include <variant>

namespace dfh_node {

UnifiedGate::UnifiedGate(
    AuthService& auth_service,
    RateLimiter& rate_limiter,
    WsConnectionLimiter& ws_limiter)
    : m_auth_service(auth_service)
    , m_rate_limiter(rate_limiter)
    , m_ws_limiter(ws_limiter) {}

GateResult UnifiedGate::authorize_http(const std::string& token, TaskKind kind) {
    GateResult result = m_auth_service.authorize(token, kind);
    if (std::holds_alternative<GateError>(result)) {
        return result;
    }

    const AuthContext& context = std::get<AuthContext>(result);
    if (!m_rate_limiter.check_and_record(context.fingerprint, context.rps_limit)) {
        return GateError{GateErrorCode::RateLimited, "Rate limit exceeded"};
    }

    return context;
}

GateResult UnifiedGate::authorize_ws_upgrade(const std::string& token) {
    GateResult result = m_auth_service.authenticate_token(token);
    if (std::holds_alternative<GateError>(result)) {
        return result;
    }

    const AuthContext& context = std::get<AuthContext>(result);
    if (!m_rate_limiter.check_and_record(context.fingerprint, context.rps_limit)) {
        return GateError{GateErrorCode::RateLimited, "Rate limit exceeded"};
    }

    if (!m_ws_limiter.register_connection(context.fingerprint, context.ws_max_connections)) {
        return GateError{GateErrorCode::ConnectionLimited, "WebSocket connection limit exceeded"};
    }

    return context;
}

GateResult UnifiedGate::authorize_ws_message(const std::string& fingerprint, TaskKind kind) {
    GateResult result = m_auth_service.authorize_fingerprint(fingerprint, kind);
    if (std::holds_alternative<GateError>(result)) {
        return result;
    }

    const AuthContext& context = std::get<AuthContext>(result);
    if (!m_rate_limiter.check_and_record(fingerprint, context.rps_limit)) {
        return GateError{GateErrorCode::RateLimited, "Rate limit exceeded"};
    }

    return context;
}

void UnifiedGate::ws_connection_closed(const std::string& fingerprint) {
    m_ws_limiter.unregister_connection(fingerprint);
}

} // namespace dfh_node
