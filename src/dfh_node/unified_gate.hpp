/**
 * \file unified_gate.hpp
 * \brief Единый gate для авторизации HTTP и WebSocket запросов.
 * \details Оркестрирует AuthService, RateLimiter и WsConnectionLimiter.
 */
#pragma once

#include "auth_service.hpp"
#include "rate_limiter.hpp"
#include "ws_connection_limiter.hpp"

namespace dfh_node {

/// \brief Единая точка авторизации для HTTP и WS сценариев.
class UnifiedGate {
public:
    /// \brief Конструктор.
    /// \param auth_service Сервис аутентификации/авторизации.
    /// \param rate_limiter Лимитер запросов.
    /// \param ws_limiter Лимитер WS-соединений.
    UnifiedGate(
        AuthService& auth_service,
        RateLimiter& rate_limiter,
        WsConnectionLimiter& ws_limiter);

    /// \brief Авторизует HTTP-запрос по токену и типу операции.
    /// \param token Plaintext токен.
    /// \param kind Тип операции.
    /// \return AuthContext при успехе или GateError при неуспехе.
    GateResult authorize_http(const std::string& token, TaskKind kind);

    /// \brief Авторизует WS upgrade только по токену (без kind).
    /// \param token Plaintext токен.
    /// \return AuthContext при успехе или GateError при неуспехе.
    GateResult authorize_ws_upgrade(const std::string& token);

    /// \brief Авторизует WS сообщение по fingerprint и типу операции.
    /// \param fingerprint HMAC-SHA256(server_secret, token) в hex.
    /// \param kind Тип операции.
    /// \return AuthContext при успехе или GateError при неуспехе.
    GateResult authorize_ws_message(const std::string& fingerprint, TaskKind kind);

    /// \brief Уведомляет gate о закрытии WS-соединения.
    /// \param fingerprint HMAC-SHA256(server_secret, token) в hex.
    void ws_connection_closed(const std::string& fingerprint);

private:
    AuthService& m_auth_service;
    RateLimiter& m_rate_limiter;
    WsConnectionLimiter& m_ws_limiter;
};

} // namespace dfh_node
