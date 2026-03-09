/// \file composite_api_key_store.hpp
/// \brief Комбинированное хранилище API-ключей.
/// \details Сначала читает bootstrap-ключи из config, затем делает fallback в MDBX.

#pragma once

#include "api_key_store.hpp"
#include "mdbx_api_key_store.hpp"

namespace dfh_node {

/// \brief Config-priority и MDBX-fallback хранилище API-ключей.
class CompositeApiKeyStore final : public IApiKeyStore {
public:
    /// \brief Создаёт композицию из bootstrap-конфига и MDBX-хранилища.
    /// \param config_store Immutable store ключей из конфигурации.
    /// \param mdbx_store MDBX-хранилище динамических ключей.
    CompositeApiKeyStore(const IApiKeyStore &config_store, const MdbxApiKeyStore &mdbx_store);

    /// \brief Ищет API-ключ сначала в config, затем в MDBX.
    /// \param fingerprint HMAC-SHA256(server_secret, token) в hex.
    /// \return Запись ключа при успехе, иначе `std::nullopt`.
    std::optional<ApiKeyRecord> lookup(const std::string &fingerprint) const override;

private:
    const IApiKeyStore &m_config;
    const MdbxApiKeyStore &m_mdbx;
};

} // namespace dfh_node
