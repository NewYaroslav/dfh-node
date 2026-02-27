/// \file auth_service.hpp
/// \brief Сервис аутентификации/авторизации и типы ошибок gate.
/// \details Поддерживает три сценария: аутентификация только по токену,
/// авторизация токена по scope и авторизация fingerprint по scope.
///
#pragma once

#include "auth_cache.hpp"
#include "config/api_key_store.hpp"
#include "core/task.hpp"
#include "security/fingerprint_computer.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace dfh_node {

/// \brief Код ошибки авторизации.
enum class GateErrorCode : std::uint8_t {
    Unauthorized = 0,             ///< Токен невалиден или истёк.
    Forbidden = 1,                ///< Недостаточно scope для операции.
    RateLimited = 2,              ///< Превышен rate-limit.
    ConnectionLimited = 3,        ///< Превышен лимит WS-соединений.
    UnsupportedOperation = 4,     ///< Неизвестный/неподдерживаемый тип операции.
    AntiReplayFailed = 5,         ///< Ошибка anti-replay (skew/signature/nonce reuse).
    AntiReplayRequired = 6,       ///< Anti-replay отключен, но обязателен для scope.
    MissingAntiReplayHeaders = 7, ///< Нет обязательных anti-replay HTTP заголовков.
    MissingAntiReplayFields = 8   ///< Нет обязательных anti-replay WS полей.
};

/// \brief Ошибка авторизации без HTTP-статуса.
struct GateError {
    GateErrorCode code;  ///< Машиночитаемый код ошибки.
    std::string message; ///< Человекочитаемое описание.
};

/// \brief Результат авторизации/gate-проверки.
/// \details std::monostate используется в anti-replay проверках без AuthContext.
using GateResult = std::variant<std::monostate, AuthContext, GateError>;

/// \brief Сервис аутентификации и проверки прав.
class AuthService {
public:
    /// \brief Конструктор.
    /// \param store Хранилище API-ключей.
    /// \param cache Кэш авторизационных контекстов.
    /// \param fingerprint_computer Вычислитель fingerprint из открытого токена.
    AuthService(IApiKeyStore &store, AuthCache &cache, const FingerprintComputer &fingerprint_computer);

    /// \brief Аутентифицирует токен без проверки scope.
    /// \param token Открытый токен.
    /// \return AuthContext при успехе или GateError при неуспехе.
    GateResult authenticate_token(const std::string &token);

    /// \brief Авторизует токен с проверкой scope для операции.
    /// \param token Открытый токен.
    /// \param kind Тип операции.
    /// \return AuthContext при успехе или GateError при неуспехе.
    GateResult authorize(const std::string &token, TaskKind kind);

    /// \brief Авторизует запрос по fingerprint с проверкой scope.
    /// \param fingerprint HMAC-SHA256(server_secret, token) в hex.
    /// \param kind Тип операции.
    /// \return AuthContext при успехе или GateError при неуспехе.
    GateResult authorize_fingerprint(const std::string &fingerprint, TaskKind kind);

private:
    IApiKeyStore &m_store;
    AuthCache &m_cache;
    const FingerprintComputer &m_fingerprint_computer;

    GateResult lookup_and_validate(const std::string &fingerprint);
    GateResult check_scope(const AuthContext &context, TaskKind kind);
    std::int64_t system_clock_epoch_ms() const;
};

} // namespace dfh_node
