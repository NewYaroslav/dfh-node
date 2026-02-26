/// \file config_api_key_store.cpp
/// \brief Реализация immutable хранилища API-ключей из конфигурации.
/// \details На этапе построения копирует данные в map для быстрых lookup.
///
#include "config_api_key_store.hpp"

namespace dfh_node {

ConfigApiKeyStore::ConfigApiKeyStore(const std::vector<config::ApiKeyEntry> &entries) {
    for (const auto &entry : entries) {
        ApiKeyRecord record{entry.fingerprint, entry.scope_mask, entry.expires_at_ms, entry.rps_limit,
                            entry.ws_max_connections};
        m_records[entry.fingerprint] = record;
    }
}

std::optional<ApiKeyRecord> ConfigApiKeyStore::lookup(const std::string &fingerprint) const {
    const auto it = m_records.find(fingerprint);
    if (it == m_records.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace dfh_node
