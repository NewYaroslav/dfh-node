/// \file anti_replay_validator.hpp
/// \brief Валидатор anti-replay: skew, подпись и уникальность nonce.
/// \details Порядок проверок: parse -> skew -> signature -> nonce store.
///
#pragma once

#include "auth_service.hpp"
#include "canonical_request.hpp"
#include "config.hpp"
#include "interfaces.hpp"
#include "nonce_store.hpp"

#include <cstddef>
#include <string>

namespace dfh_node {

/// \brief Валидирует anti-replay для HTTP и WS запросов.
class AntiReplayValidator {
public:
    /// \brief Создаёт валидатор anti-replay.
    /// \param config Конфигурация anti-replay.
    /// \param system_clock Источник серверного времени.
    /// \param nonce_store Хранилище использованных nonce.
    AntiReplayValidator(const config::AntiReplayConfig &config, IClock &system_clock, NonceStore &nonce_store);

    /// \brief Проверяет anti-replay поля HTTP-запроса.
    /// \param fingerprint Fingerprint токена.
    /// \param signing_key Ключ подписи (SHA256(token), raw bytes).
    /// \param key_len Длина ключа подписи в байтах (ожидается 32).
    /// \param input Поля HTTP для canonical string.
    /// \param signature Подпись в hex lowercase.
    /// \return `std::monostate` при успехе или `GateError` при отказе.
    GateResult validate_http(const std::string &fingerprint, const unsigned char *signing_key, std::size_t key_len,
                             const HttpCanonicalInput &input, const std::string &signature);

    /// \brief Проверяет anti-replay поля WS control-message.
    /// \param fingerprint Fingerprint токена.
    /// \param signing_key Ключ подписи (SHA256(token), raw bytes).
    /// \param key_len Длина ключа подписи в байтах (ожидается 32).
    /// \param input Поля WS для canonical string.
    /// \param signature Подпись в hex lowercase.
    /// \return `std::monostate` при успехе или `GateError` при отказе.
    GateResult validate_ws(const std::string &fingerprint, const unsigned char *signing_key, std::size_t key_len,
                           const WsCanonicalInput &input, const std::string &signature);

private:
    /// \brief Общая проверка anti-replay после формирования canonical string.
    /// \param fingerprint Fingerprint токена.
    /// \param signing_key Ключ подписи (raw bytes).
    /// \param key_len Длина ключа подписи.
    /// \param canonical Каноническая строка.
    /// \param timestamp Unix epoch ms в строковом виде.
    /// \param nonce Nonce в hex lowercase.
    /// \param signature Подпись в hex lowercase.
    /// \return `std::monostate` при успехе или `GateError` при отказе.
    GateResult validate_common(const std::string &fingerprint, const unsigned char *signing_key, std::size_t key_len,
                               const std::string &canonical, const std::string &timestamp, const std::string &nonce,
                               const std::string &signature);

    const config::AntiReplayConfig &m_config;
    IClock &m_system_clock;
    NonceStore &m_nonce_store;
};

} // namespace dfh_node
