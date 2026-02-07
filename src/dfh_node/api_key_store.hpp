/**
 * \file api_key_store.hpp
 * \brief Контракты и записи хранилища API-ключей.
 * \details Определяет независимый от config store-record для авторизации.
 */
#pragma once

#include "scope.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace dfh_node {

/// \brief Запись API-ключа в хранилище.
/// \details Отделена от конфигурационных структур для стабильного store-контракта.
struct ApiKeyRecord {
    std::string fingerprint; ///< HMAC-SHA256(server_secret, token) в hex.
    ScopeMask scope_mask = 0; ///< Битовая маска разрешённых scope.
    std::optional<std::int64_t> expires_at_ms; ///< Время истечения Unix epoch ms.
    std::int64_t rps_limit = 0; ///< Лимит запросов в секунду.
    std::int64_t ws_max_connections = 0; ///< Лимит одновременных WS-соединений.
};

/// \brief Интерфейс хранилища API-ключей.
class IApiKeyStore {
public:
    virtual ~IApiKeyStore() = default;

    /// \brief Ищет API-ключ по fingerprint.
    /// \param fingerprint HMAC-SHA256(server_secret, token) в hex.
    /// \return Запись ключа при успехе, иначе std::nullopt.
    virtual std::optional<ApiKeyRecord> lookup(const std::string& fingerprint) const = 0;
};

} // namespace dfh_node
