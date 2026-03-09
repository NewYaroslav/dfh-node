/// \file composite_api_key_store.cpp
/// \brief Реализация комбинированного хранилища API-ключей.
/// \details Сохраняет приоритет bootstrap-ключей из config над динамическими MDBX-записями.

#include "composite_api_key_store.hpp"

namespace dfh_node {

CompositeApiKeyStore::CompositeApiKeyStore(const IApiKeyStore &config_store, const MdbxApiKeyStore &mdbx_store)
    : m_config(config_store), m_mdbx(mdbx_store) {}

std::optional<ApiKeyRecord> CompositeApiKeyStore::lookup(const std::string &fingerprint) const {
    if (const std::optional<ApiKeyRecord> record = m_config.lookup(fingerprint); record.has_value()) {
        return record;
    }

    return m_mdbx.lookup(fingerprint);
}

} // namespace dfh_node
