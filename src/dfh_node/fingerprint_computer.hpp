/**
 * \file fingerprint_computer.hpp
 * \brief Декларация вычислителя fingerprint для API-токенов.
 * \details Формирует стабильный HMAC-SHA256 в hex в нижнем регистре для хранения и
 * сравнения.
 */
#pragma once

#include <string>

namespace dfh_node {

/// \brief Вычисляет fingerprint токена через HMAC-SHA256.
/// \details Формат результата: 64 символа hex в нижнем регистре.
class FingerprintComputer {
  public:
    /// \brief Создаёт вычислитель с серверным секретом.
    /// \param server_secret Секрет из config.security.server_secret.
    explicit FingerprintComputer(const std::string &server_secret);

    /// \brief Вычисляет fingerprint для Открытый-токена.
    /// \param token Токен клиента в открытом виде (не логировать).
    /// \return HMAC-SHA256(token) в hex в нижнем регистре длиной 64 символа.
    std::string compute(const std::string &token) const;

  private:
    std::string m_server_secret;
};

} // namespace dfh_node
