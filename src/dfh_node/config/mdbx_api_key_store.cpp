/// \file mdbx_api_key_store.cpp
/// \brief Реализация MDBX-хранилища API-ключей.
/// \details Инкапсулирует сериализацию msgpack и поддержку трёх индексов.

#include "mdbx_api_key_store.hpp"

#include <msgpack.hpp>

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace dfh_node {
namespace {

constexpr const char *k_keys_by_id = "keys_by_id";
constexpr const char *k_keys_by_fingerprint = "keys_by_fingerprint";
constexpr const char *k_keys_by_name = "keys_by_name";
constexpr unsigned k_max_maps = 8;

std::int64_t now_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string pack_record(const MdbxKeyRecord &record) {
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, record);
    return std::string(buffer.data(), buffer.size());
}

MdbxKeyRecord unpack_record(const mdbx::slice &value) {
    if (!value) {
        throw std::runtime_error("MDBX record is missing");
    }

    msgpack::object_handle object =
        msgpack::unpack(static_cast<const char *>(value.iov_base), static_cast<std::size_t>(value.iov_len));
    return object.get().as<MdbxKeyRecord>();
}

} // namespace

MdbxApiKeyStore::MdbxApiKeyStore(std::string env_path) : m_env_path(std::move(env_path)) {}

void MdbxApiKeyStore::open() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_env) {
        return;
    }

    const std::filesystem::path env_path(m_env_path);
    const std::filesystem::path parent_path = env_path.has_parent_path() ? env_path.parent_path() : env_path;
    if (!parent_path.empty()) {
        std::filesystem::create_directories(parent_path);
    }

    mdbx::env::operate_parameters operate_parameters{k_max_maps};
    mdbx::env_managed::create_parameters create_parameters;
    create_parameters.use_subdirectory = false;

    m_env = mdbx::env_managed(m_env_path, create_parameters, operate_parameters);

    auto txn = m_env.start_write();
    m_dbi_by_id = txn.create_map(k_keys_by_id);
    m_dbi_by_fingerprint = txn.create_map(k_keys_by_fingerprint);
    m_dbi_by_name = txn.create_map(k_keys_by_name);
    txn.commit();
}

std::optional<ApiKeyRecord> MdbxApiKeyStore::lookup(const std::string &fingerprint) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_env) {
        return std::nullopt;
    }

    const auto txn = m_env.start_read();
    const std::optional<MdbxKeyRecord> record = get_by_key(txn, m_dbi_by_fingerprint, fingerprint);
    if (!record.has_value()) {
        return std::nullopt;
    }

    const std::int64_t now_ms = now_epoch_ms();
    if (record->revoked || is_expired(*record, now_ms)) {
        return std::nullopt;
    }

    return to_api_key_record(*record);
}

void MdbxApiKeyStore::put(const MdbxKeyRecord &rec) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_env) {
        throw std::runtime_error("MDBX environment is not opened");
    }

    auto txn = m_env.start_write();
    const std::optional<MdbxKeyRecord> existing_by_id = get_by_key(txn, m_dbi_by_id, rec.id);
    const std::optional<MdbxKeyRecord> existing_by_name = get_by_key(txn, m_dbi_by_name, rec.name);
    const std::optional<MdbxKeyRecord> existing_by_fingerprint = get_by_key(txn, m_dbi_by_fingerprint, rec.fingerprint);

    if (existing_by_name.has_value() && existing_by_name->id != rec.id) {
        throw std::runtime_error("duplicate name");
    }
    if (existing_by_fingerprint.has_value() && existing_by_fingerprint->id != rec.id) {
        throw std::runtime_error("duplicate fingerprint");
    }

    if (existing_by_id.has_value()) {
        if (existing_by_id->name != rec.name) {
            txn.erase(m_dbi_by_name, mdbx::slice(existing_by_id->name));
        }
        if (existing_by_id->fingerprint != rec.fingerprint) {
            txn.erase(m_dbi_by_fingerprint, mdbx::slice(existing_by_id->fingerprint));
        }
    }

    const std::string packed = pack_record(rec);
    const mdbx::slice value(packed);
    txn.upsert(m_dbi_by_id, mdbx::slice(rec.id), value);
    txn.upsert(m_dbi_by_fingerprint, mdbx::slice(rec.fingerprint), value);
    txn.upsert(m_dbi_by_name, mdbx::slice(rec.name), value);
    txn.commit();
}

std::optional<MdbxKeyRecord> MdbxApiKeyStore::get_by_id(const std::string &id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_env) {
        return std::nullopt;
    }

    const auto txn = m_env.start_read();
    return get_by_key(txn, m_dbi_by_id, id);
}

std::optional<MdbxKeyRecord> MdbxApiKeyStore::get_by_name(const std::string &name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_env) {
        return std::nullopt;
    }

    const auto txn = m_env.start_read();
    return get_by_key(txn, m_dbi_by_name, name);
}

bool MdbxApiKeyStore::remove(const std::string &id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_env) {
        return false;
    }

    auto txn = m_env.start_write();
    const std::optional<MdbxKeyRecord> existing = get_by_key(txn, m_dbi_by_id, id);
    if (!existing.has_value()) {
        return false;
    }

    txn.erase(m_dbi_by_id, mdbx::slice(existing->id));
    txn.erase(m_dbi_by_fingerprint, mdbx::slice(existing->fingerprint));
    txn.erase(m_dbi_by_name, mdbx::slice(existing->name));
    txn.commit();
    return true;
}

std::vector<MdbxKeyRecord> MdbxApiKeyStore::list_all() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<MdbxKeyRecord> result;
    if (!m_env) {
        return result;
    }

    const auto txn = m_env.start_read();
    auto cursor = txn.open_cursor(m_dbi_by_id);
    for (auto value = cursor.to_first(false); value.done; value = cursor.to_next(false)) {
        result.push_back(unpack_record(value.value));
    }
    return result;
}

bool MdbxApiKeyStore::is_healthy() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_env) {
        return false;
    }

    try {
        const auto txn = m_env.start_read();
        (void)txn;
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<MdbxKeyRecord> MdbxApiKeyStore::get_by_key(const mdbx::txn &txn, const mdbx::map_handle map,
                                                         const std::string &key) const {
    const mdbx::slice value = txn.get(map, mdbx::slice(key), mdbx::slice());
    if (!value) {
        return std::nullopt;
    }

    return unpack_record(value);
}

ApiKeyRecord MdbxApiKeyStore::to_api_key_record(const MdbxKeyRecord &record) {
    return ApiKeyRecord{record.fingerprint, record.scope_mask, record.expires_at_ms, record.rps_limit,
                        record.ws_max_connections};
}

bool MdbxApiKeyStore::is_expired(const MdbxKeyRecord &record, const std::int64_t now_ms) {
    return record.expires_at_ms.has_value() && now_ms > *record.expires_at_ms;
}

} // namespace dfh_node
