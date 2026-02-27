/// \file anti_replay_validator.hpp
/// \brief Валидатор anti-replay: проверка времени, подписи и уникальности nonce.
/// \details Порядок проверок: разбор полей -> окно времени -> подпись -> nonce-хранилище.
///
#pragma once

#include "auth/auth_service.hpp"
#include "canonical_request.hpp"
#include "config/config.hpp"
#include "core/interfaces.hpp"
#include "nonce_store.hpp"

#include <cstddef>
#include <string>

namespace dfh_node {

/// \brief Валидирует anti-replay для HTTP- и WS-запросов.
class AntiReplayValidator {
public:
    /// \brief Создаёт валидатор anti-replay.
    /// \param config Конфигурация anti-replay.
    /// \param system_clock Источник серверного времени.
    /// \param nonce_store Хранилище использованных nonce.
    AntiReplayValidator(const config::AntiReplayConfig &config, IClock &system_clock, NonceStore &nonce_store);

    /// \brief Проверяет anti-replay поля HTTP-запроса.
    /// \param fingerprint Отпечаток токена.
    /// \param signing_key Ключ подписи (SHA256(token), сырые байты).
    /// \param key_len Длина ключа подписи в байтах (ожидается 32).
    /// \param input Поля HTTP для формирования канонической строки.
    /// \param signature Подпись в hex в нижнем регистре.
    /// \return `std::monostate` при успехе или `GateError` при отказе.
    GateResult validate_http(const std::string &fingerprint, const unsigned char *signing_key, std::size_t key_len,
                             const HttpCanonicalInput &input, const std::string &signature);

    /// \brief Проверяет anti-replay поля WS-управляющего сообщения.
    /// \param fingerprint Отпечаток токена.
    /// \param signing_key Ключ подписи (SHA256(token), сырые байты).
    /// \param key_len Длина ключа подписи в байтах (ожидается 32).
    /// \param input Поля WS для формирования канонической строки.
    /// \param signature Подпись в hex в нижнем регистре.
    /// \return `std::monostate` при успехе или `GateError` при отказе.
    GateResult validate_ws(const std::string &fingerprint, const unsigned char *signing_key, std::size_t key_len,
                           const WsCanonicalInput &input, const std::string &signature);

private:
    /// \brief Общая проверка anti-replay после формирования канонической строки.
    /// \param fingerprint Отпечаток токена.
    /// \param signing_key Ключ подписи (сырые байты).
    /// \param key_len Длина ключа подписи.
    /// \param canonical Каноническая строка.
    /// \param timestamp Unix epoch в миллисекундах в строковом виде.
    /// \param nonce Значение nonce в hex в нижнем регистре.
    /// \param signature Подпись в hex в нижнем регистре.
    /// \return `std::monostate` при успехе или `GateError` при отказе.
    GateResult validate_common(const std::string &fingerprint, const unsigned char *signing_key, std::size_t key_len,
                               const std::string &canonical, const std::string &timestamp, const std::string &nonce,
                               const std::string &signature);

    const config::AntiReplayConfig &m_config;
    IClock &m_system_clock;
    NonceStore &m_nonce_store;
};

} // namespace dfh_node
