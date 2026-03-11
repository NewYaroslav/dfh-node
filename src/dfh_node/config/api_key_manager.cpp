/// \file api_key_manager.cpp
/// \brief Реализация менеджера динамических API-ключей.
/// \details Выполняет только синхронные операции поверх `MdbxApiKeyStore` и
/// после каждой мутации инвалидирует `AuthCache`.

#include "api_key_manager.hpp"

#include "core/time_utils.hpp"

#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace dfh_node {
namespace {

constexpr char k_hex_digits[] = "0123456789abcdef";

std::string bytes_to_hex(const unsigned char *data, const std::size_t size) {
    std::string result;
    result.reserve(size * 2U);
    for (std::size_t index = 0; index < size; ++index) {
        const unsigned char byte = data[index];
        result.push_back(k_hex_digits[(byte >> 4U) & 0x0FU]);
        result.push_back(k_hex_digits[byte & 0x0FU]);
    }
    return result;
}

} // namespace

ApiKeyManager::ApiKeyManager(MdbxApiKeyStore &store, AuthCache &cache, const FingerprintComputer &fp_computer,
                             const config::AuthConfig &auth_cfg)
    : m_store(store), m_cache(cache), m_fp(fp_computer), m_auth_cfg(auth_cfg) {}

CreateKeyResult ApiKeyManager::create(const std::string &name, const ScopeMask scope_mask, const std::int64_t rps_limit,
                                      const std::int64_t ws_max_connections,
                                      const std::optional<std::int64_t> expires_at_ms) {
    if (m_store.get_by_name(name).has_value()) {
        throw std::runtime_error("duplicate name");
    }

    CreateKeyResult result;
    result.id = generate_uuid();
    result.token = generate_token();
    result.record.id = result.id;
    result.record.name = name;
    result.record.fingerprint = m_fp.compute(result.token);
    result.record.scope_mask = scope_mask;
    result.record.rps_limit = rps_limit;
    result.record.ws_max_connections = ws_max_connections;
    result.record.expires_at_ms = expires_at_ms;
    result.record.revoked = false;
    result.record.created_at_ms = dfh_node::now_epoch_ms();
    result.record.updated_at_ms = result.record.created_at_ms;

    m_store.put(result.record);
    return result;
}

std::optional<MdbxKeyRecord> ApiKeyManager::get_by_id(const std::string &id) const { return m_store.get_by_id(id); }

std::vector<MdbxKeyRecord> ApiKeyManager::list(const bool include_revoked) const {
    std::vector<MdbxKeyRecord> records = m_store.list_all();
    if (include_revoked) {
        return records;
    }

    records.erase(
        std::remove_if(records.begin(), records.end(), [](const MdbxKeyRecord &record) { return record.revoked; }),
        records.end());
    return records;
}

bool ApiKeyManager::update(const std::string &id, const UpdateKeyRequest &req) {
    std::optional<MdbxKeyRecord> record = m_store.get_by_id(id);
    if (!record.has_value()) {
        return false;
    }

    if (req.name.has_value()) {
        record->name = *req.name;
    }
    if (req.scope_mask.has_value()) {
        record->scope_mask = *req.scope_mask;
    }
    if (req.rps_limit.has_value()) {
        record->rps_limit = *req.rps_limit;
    }
    if (req.ws_max_connections.has_value()) {
        record->ws_max_connections = *req.ws_max_connections;
    }
    if (req.expires_at_ms.has_value()) {
        record->expires_at_ms = *req.expires_at_ms;
    }

    record->updated_at_ms = dfh_node::now_epoch_ms();
    m_store.put(*record);
    m_cache.invalidate(record->fingerprint);
    return true;
}

bool ApiKeyManager::revoke(const std::string &id, bool &already_revoked) {
    std::optional<MdbxKeyRecord> record = m_store.get_by_id(id);
    if (!record.has_value()) {
        already_revoked = false;
        return false;
    }

    already_revoked = record->revoked;
    if (record->revoked) {
        return true;
    }

    record->revoked = true;
    record->updated_at_ms = dfh_node::now_epoch_ms();
    m_store.put(*record);
    m_cache.invalidate(record->fingerprint);
    return true;
}

bool ApiKeyManager::remove(const std::string &id) {
    const std::optional<MdbxKeyRecord> record = m_store.get_by_id(id);
    if (!record.has_value()) {
        return false;
    }

    if (!m_store.remove(id)) {
        return false;
    }

    m_cache.invalidate(record->fingerprint);
    return true;
}

std::string ApiKeyManager::generate_token() const {
    std::array<unsigned char, 32> token_bytes{};
    if (RAND_bytes(token_bytes.data(), static_cast<int>(token_bytes.size())) != 1) {
        throw std::runtime_error("RAND_bytes failed for token");
    }

    std::string token = bytes_to_hex(token_bytes.data(), token_bytes.size());
    OPENSSL_cleanse(token_bytes.data(), token_bytes.size());
    return token;
}

std::string ApiKeyManager::generate_uuid() const {
    std::array<unsigned char, 16> uuid_bytes{};
    if (RAND_bytes(uuid_bytes.data(), static_cast<int>(uuid_bytes.size())) != 1) {
        throw std::runtime_error("RAND_bytes failed for uuid");
    }

    uuid_bytes[6] = static_cast<unsigned char>((uuid_bytes[6] & 0x0FU) | 0x40U);
    uuid_bytes[8] = static_cast<unsigned char>((uuid_bytes[8] & 0x3FU) | 0x80U);

    const std::string hex = bytes_to_hex(uuid_bytes.data(), uuid_bytes.size());
    OPENSSL_cleanse(uuid_bytes.data(), uuid_bytes.size());

    std::string uuid;
    uuid.reserve(36U);
    uuid.append(hex, 0, 8);
    uuid.push_back('-');
    uuid.append(hex, 8, 4);
    uuid.push_back('-');
    uuid.append(hex, 12, 4);
    uuid.push_back('-');
    uuid.append(hex, 16, 4);
    uuid.push_back('-');
    uuid.append(hex, 20, 12);
    return uuid;
}

} // namespace dfh_node
