/// \file auth_service.cpp
/// \brief Реализация сервиса аутентификации/авторизации.
/// \details Сначала выполняет lookup в cache/store, затем проверяет срок
/// действия и scope.
///
#include "auth_service.hpp"

#include <chrono>

namespace dfh_node {

AuthService::AuthService(IApiKeyStore &store, AuthCache &cache, const FingerprintComputer &fingerprint_computer)
    : m_store(store), m_cache(cache), m_fingerprint_computer(fingerprint_computer) {}

std::int64_t AuthService::system_clock_epoch_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

GateResult AuthService::authenticate_token(const std::string &token) {
    const std::string fingerprint = m_fingerprint_computer.compute(token);
    return lookup_and_validate(fingerprint);
}

GateResult AuthService::authorize(const std::string &token, TaskKind kind) {
    GateResult auth_result = authenticate_token(token);
    if (std::holds_alternative<GateError>(auth_result)) {
        return auth_result;
    }

    const AuthContext &context = std::get<AuthContext>(auth_result);
    return check_scope(context, kind);
}

GateResult AuthService::authorize_fingerprint(const std::string &fingerprint, TaskKind kind) {
    GateResult lookup_result = lookup_and_validate(fingerprint);
    if (std::holds_alternative<GateError>(lookup_result)) {
        return lookup_result;
    }

    const AuthContext &context = std::get<AuthContext>(lookup_result);
    return check_scope(context, kind);
}

GateResult AuthService::lookup_and_validate(const std::string &fingerprint) {
    const auto cached_context = m_cache.get(fingerprint);
    if (cached_context.has_value()) {
        if (cached_context->expires_at_ms.has_value() &&
            cached_context->expires_at_ms.value() < system_clock_epoch_ms()) {
            return GateError{GateErrorCode::Unauthorized, "Token expired"};
        }
        return cached_context.value();
    }

    const auto record = m_store.lookup(fingerprint);
    if (!record.has_value()) {
        return GateError{GateErrorCode::Unauthorized, "Invalid token"};
    }

    if (record->expires_at_ms.has_value() && record->expires_at_ms.value() < system_clock_epoch_ms()) {
        return GateError{GateErrorCode::Unauthorized, "Token expired"};
    }

    const AuthContext context{fingerprint, record->scope_mask, record->rps_limit, record->ws_max_connections,
                              record->expires_at_ms};
    m_cache.put(fingerprint, context);
    return context;
}

GateResult AuthService::check_scope(const AuthContext &context, TaskKind kind) {
    const auto required = required_scope(kind);
    if (!required.has_value()) {
        return GateError{GateErrorCode::UnsupportedOperation, "Unknown operation type"};
    }

    if (!has_scope(context.scope_mask, required.value())) {
        return GateError{GateErrorCode::Forbidden, "Insufficient scope"};
    }

    return context;
}

} // namespace dfh_node
