/// \file unified_gate.hpp
/// \brief Единая точка gate-валидации для HTTP и WS.
/// \details Объединяет auth, rate limit, WS connection limit и anti-replay.
///
#pragma once

#include "auth_service.hpp"
#include "rate_limiter.hpp"
#include "security/anti_replay_fields.hpp"
#include "security/anti_replay_validator.hpp"
#include "ws_connection_limiter.hpp"

#include <cstddef>
#include <string>

namespace dfh_node {

/// \brief Единый gate для HTTP/WS операций.
class UnifiedGate {
public:
    /// \brief Создаёт gate с зависимостями авторизации и лимитов.
    /// \param auth_service Сервис аутентификации/авторизации.
    /// \param rate_limiter Лимитер запросов.
    /// \param ws_limiter Лимитер WS-соединений.
    /// \param anti_replay_validator Валидатор anti-replay (nullable).
    /// \param require_for_scopes Битмаска scope, где anti-replay обязателен при отключенном валидаторе.
    UnifiedGate(AuthService &auth_service, RateLimiter &rate_limiter, WsConnectionLimiter &ws_limiter,
                AntiReplayValidator *anti_replay_validator = nullptr,
                ScopeMask require_for_scopes = to_scope_mask(Scope::Write) | to_scope_mask(Scope::Admin) |
                                               to_scope_mask(Scope::Sync));

    /// \brief Авторизует HTTP-запрос по токену и типу операции.
    /// \param token Открытый токен.
    /// \param kind Тип операции.
    /// \param ar_fields Anti-replay поля (если anti-replay включён).
    /// \return `AuthContext` при успехе или `GateError` при отказе.
    GateResult authorize_http(const std::string &token, TaskKind kind, const HttpAntiReplayFields *ar_fields = nullptr);

    /// \brief Авторизует WS upgrade только по токену.
    /// \param token Открытый токен.
    /// \return `AuthContext` при успехе или `GateError` при отказе.
    GateResult authorize_ws_upgrade(const std::string &token);

    /// \brief Авторизует WS-сообщение по fingerprint и типу операции.
    /// \param fingerprint HMAC-SHA256(server_secret, token) в hex.
    /// \param kind Тип операции.
    /// \param signing_key Ключ подписи (32 сырые байты из контекста WS-сессии).
    /// \param key_len Длина ключа подписи в байтах.
    /// \param ar_fields Anti-replay поля (если anti-replay включён).
    /// \return `AuthContext` при успехе или `GateError` при отказе.
    GateResult authorize_ws_message(const std::string &fingerprint, TaskKind kind,
                                    const unsigned char *signing_key = nullptr, std::size_t key_len = 0,
                                    const WsAntiReplayFields *ar_fields = nullptr);

    /// \brief Снимает учёт активного WS-соединения при закрытии.
    /// \param fingerprint Fingerprint клиента.
    void ws_connection_closed(const std::string &fingerprint);

private:
    /// \brief Проверяет обязательность anti-replay для операции.
    /// \param kind Тип операции.
    /// \return `std::monostate` если проверка пройдена, иначе `GateError`.
    GateResult validate_anti_replay_requirement(TaskKind kind) const;

    AuthService &m_auth_service;                  ///< Сервис авторизации.
    RateLimiter &m_rate_limiter;                  ///< Лимитер частоты запросов.
    WsConnectionLimiter &m_ws_limiter;            ///< Лимитер WS-соединений.
    AntiReplayValidator *m_anti_replay_validator; ///< Nullable при отключённом anti-replay.
    ScopeMask m_require_for_scopes;               ///< Scope, для которых anti-replay обязателен.
};

} // namespace dfh_node
