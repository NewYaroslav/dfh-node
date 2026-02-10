/// \file config_api_key_store.hpp
/// \brief Immutable-реализация хранилища API-ключей из конфигурации.
/// \details Заполняется один раз в конструкторе и далее читается без mutex.

#pragma once

#include "api_key_store.hpp"
#include "config.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace dfh_node {

/// \brief Реализация IApiKeyStore на базе записей из config.
class ConfigApiKeyStore final : public IApiKeyStore {
public:
    /// \brief Строит store из списка ключей конфигурации.
    /// \param entries Записи config::ApiKeyEntry.
    explicit ConfigApiKeyStore(const std::vector<config::ApiKeyEntry> &entries);

    /// \brief Ищет ключ по fingerprint.
    /// \param fingerprint HMAC-SHA256(server_secret, token) в hex.
    /// \return Запись ключа, если fingerprint найден, иначе std::nullopt.
    std::optional<ApiKeyRecord> lookup(const std::string &fingerprint) const override;

private:
    std::unordered_map<std::string, ApiKeyRecord> m_records;
};

} // namespace dfh_node
