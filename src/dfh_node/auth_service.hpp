/**
 * \file auth_service.hpp
 * \brief Сервис аутентификации/авторизации и типы ошибок gate.
 * \details Поддерживает три сценария: token-only auth, token+scope auth и fingerprint+scope auth.
 */
#pragma once

#include "api_key_store.hpp"
#include "auth_cache.hpp"
#include "fingerprint_computer.hpp"
#include "task.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace dfh_node {

/// \brief Код ошибки авторизации.
enum class GateErrorCode : std::uint8_t {
    Unauthorized,        ///< Токен невалиден или истёк.
    Forbidden,           ///< Недостаточно scope для операции.
    RateLimited,         ///< Превышен rate-limit.
    ConnectionLimited,   ///< Превышен лимит WS-соединений.
    UnsupportedOperation ///< Неизвестный/неподдерживаемый тип операции.
};

/// \brief Ошибка авторизации без HTTP-статуса.
struct GateError {
    GateErrorCode code; ///< Машиночитаемый код ошибки.
    std::string message; ///< Человекочитаемое описание.
};

/// \brief Результат авторизации.
using GateResult = std::variant<AuthContext, GateError>;

/// \brief Сервис аутентификации и проверки прав.
class AuthService {
public:
    /// \brief Конструктор.
    /// \param store Хранилище API-ключей.
    /// \param cache Кэш авторизационных контекстов.
    /// \param fingerprint_computer Вычислитель fingerprint из plaintext-токена.
    AuthService(
        IApiKeyStore& store,
        AuthCache& cache,
        const FingerprintComputer& fingerprint_computer);

    /// \brief Аутентифицирует токен без проверки scope.
    /// \param token Plaintext токен.
    /// \return AuthContext при успехе или GateError при неуспехе.
    GateResult authenticate_token(const std::string& token);

    /// \brief Авторизует токен с проверкой scope для операции.
    /// \param token Plaintext токен.
    /// \param kind Тип операции.
    /// \return AuthContext при успехе или GateError при неуспехе.
    GateResult authorize(const std::string& token, TaskKind kind);

    /// \brief Авторизует запрос по fingerprint с проверкой scope.
    /// \param fingerprint HMAC-SHA256(server_secret, token) в hex.
    /// \param kind Тип операции.
    /// \return AuthContext при успехе или GateError при неуспехе.
    GateResult authorize_fingerprint(const std::string& fingerprint, TaskKind kind);

private:
    IApiKeyStore& m_store;
    AuthCache& m_cache;
    const FingerprintComputer& m_fingerprint_computer;

    GateResult lookup_and_validate(const std::string& fingerprint);
    GateResult check_scope(const AuthContext& context, TaskKind kind);
    std::int64_t system_clock_epoch_ms() const;
};

} // namespace dfh_node
