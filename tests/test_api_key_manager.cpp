/// \file test_api_key_manager.cpp
/// \brief Юнит-тесты для ApiKeyManager.
/// \details Проверяет генерацию токена и UUID, мутации ключей и инвалидацию
/// `AuthCache`.

#include "config.hpp"
#include "test_helpers.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

using namespace dfh_node;

namespace {

bool is_hex_lowercase(const std::string &value) {
    for (const char ch : value) {
        const bool is_digit = ch >= '0' && ch <= '9';
        const bool is_hex_alpha = ch >= 'a' && ch <= 'f';
        if (!is_digit && !is_hex_alpha) {
            return false;
        }
    }
    return true;
}

bool is_uuid_v4(const std::string &value) {
    if (value.size() != 36U) {
        return false;
    }
    if (value[8] != '-' || value[13] != '-' || value[18] != '-' || value[23] != '-') {
        return false;
    }
    if (value[14] != '4') {
        return false;
    }
    if (value[19] != '8' && value[19] != '9' && value[19] != 'a' && value[19] != 'b') {
        return false;
    }

    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8U || index == 13U || index == 18U || index == 23U) {
            continue;
        }
        if (!((value[index] >= '0' && value[index] <= '9') || (value[index] >= 'a' && value[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

class TempMdbxStore {
public:
    TempMdbxStore()
        : m_root(std::filesystem::temp_directory_path() /
                 ("dfh-node-api-key-manager-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
          m_env_path(m_root / "keys.mdbx") {
        std::filesystem::create_directories(m_root);
        m_store = std::make_unique<MdbxApiKeyStore>(m_env_path.string());
        m_store->open();
    }

    ~TempMdbxStore() { m_store.reset(); }

    MdbxApiKeyStore &store() { return *m_store; }

private:
    std::filesystem::path m_root;
    std::filesystem::path m_env_path;
    std::unique_ptr<MdbxApiKeyStore> m_store;
};

ApiKeyManager make_manager(MdbxApiKeyStore &store, AuthCache &cache, FingerprintComputer &fp, config::AuthConfig &cfg) {
    return ApiKeyManager(store, cache, fp, cfg);
}

void test_create_generates_token_and_uuid() {
    TempMdbxStore fixture;
    AuthCache cache(60000);
    FingerprintComputer fp("test-secret");
    config::AuthConfig auth_cfg;
    ApiKeyManager manager = make_manager(fixture.store(), cache, fp, auth_cfg);

    const CreateKeyResult result = manager.create("writer", Scope::Read | Scope::Write, 123, 7, std::nullopt);

    CHECK_EQ(result.token.size(), 64U);
    CHECK(is_hex_lowercase(result.token));
    CHECK(is_uuid_v4(result.id));
    CHECK_EQ(result.record.id, result.id);
    CHECK_EQ(result.record.name, "writer");
    CHECK_EQ(result.record.scope_mask, Scope::Read | Scope::Write);
    CHECK_EQ(result.record.rps_limit, 123);
    CHECK_EQ(result.record.ws_max_connections, 7);
    CHECK_EQ(result.record.fingerprint, fp.compute(result.token));

    const auto lookup = fixture.store().lookup(result.record.fingerprint);
    CHECK(lookup.has_value());
}

void test_create_duplicate_name_throws() {
    TempMdbxStore fixture;
    AuthCache cache(60000);
    FingerprintComputer fp("test-secret");
    config::AuthConfig auth_cfg;
    ApiKeyManager manager = make_manager(fixture.store(), cache, fp, auth_cfg);

    (void)manager.create("dup-name", static_cast<ScopeMask>(Scope::Read), 10, 1, std::nullopt);

    bool thrown = false;
    try {
        (void)manager.create("dup-name", static_cast<ScopeMask>(Scope::Write), 20, 2, std::nullopt);
    } catch (const std::runtime_error &ex) {
        thrown = std::string(ex.what()) == "duplicate name";
    }

    CHECK(thrown);
}

void test_revoke_is_idempotent() {
    TempMdbxStore fixture;
    AuthCache cache(60000);
    FingerprintComputer fp("test-secret");
    config::AuthConfig auth_cfg;
    ApiKeyManager manager = make_manager(fixture.store(), cache, fp, auth_cfg);

    const CreateKeyResult created =
        manager.create("revoke-me", static_cast<ScopeMask>(Scope::Write), 50, 3, std::nullopt);
    bool already_revoked = false;

    CHECK(manager.revoke(created.id, already_revoked));
    CHECK(!already_revoked);
    CHECK(!fixture.store().lookup(created.record.fingerprint).has_value());

    CHECK(manager.revoke(created.id, already_revoked));
    CHECK(already_revoked);
}

void test_remove_deletes_key_and_invalidates_cache() {
    TempMdbxStore fixture;
    AuthCache cache(60000);
    FingerprintComputer fp("test-secret");
    config::AuthConfig auth_cfg;
    ApiKeyManager manager = make_manager(fixture.store(), cache, fp, auth_cfg);

    const CreateKeyResult created =
        manager.create("remove-me", static_cast<ScopeMask>(Scope::Admin), 0, 0, std::nullopt);
    cache.put(created.record.fingerprint,
              AuthContext{created.record.fingerprint, created.record.scope_mask, created.record.rps_limit,
                          created.record.ws_max_connections, created.record.expires_at_ms});

    CHECK(manager.remove(created.id));
    CHECK(!fixture.store().get_by_id(created.id).has_value());
    CHECK(!fixture.store().lookup(created.record.fingerprint).has_value());
    CHECK(!cache.get(created.record.fingerprint).has_value());
    CHECK(!manager.remove(created.id));
}

void test_update_changes_limits_and_invalidates_cache() {
    TempMdbxStore fixture;
    AuthCache cache(60000);
    FingerprintComputer fp("test-secret");
    config::AuthConfig auth_cfg;
    ApiKeyManager manager = make_manager(fixture.store(), cache, fp, auth_cfg);

    const CreateKeyResult created =
        manager.create("update-me", static_cast<ScopeMask>(Scope::Read), 10, 1, std::nullopt);
    cache.put(created.record.fingerprint,
              AuthContext{created.record.fingerprint, created.record.scope_mask, created.record.rps_limit,
                          created.record.ws_max_connections, created.record.expires_at_ms});

    UpdateKeyRequest request;
    request.rps_limit = 100;
    request.ws_max_connections = 5;
    request.name = "update-me-2";

    CHECK(manager.update(created.id, request));

    const auto updated = fixture.store().get_by_id(created.id);
    CHECK(updated.has_value());
    CHECK_EQ(updated->name, "update-me-2");
    CHECK_EQ(updated->rps_limit, 100);
    CHECK_EQ(updated->ws_max_connections, 5);
    CHECK(!cache.get(created.record.fingerprint).has_value());
}

void test_list_filters_revoked_records() {
    TempMdbxStore fixture;
    AuthCache cache(60000);
    FingerprintComputer fp("test-secret");
    config::AuthConfig auth_cfg;
    ApiKeyManager manager = make_manager(fixture.store(), cache, fp, auth_cfg);

    const CreateKeyResult active =
        manager.create("active-key", static_cast<ScopeMask>(Scope::Read), 0, 0, std::nullopt);
    const CreateKeyResult revoked =
        manager.create("revoked-key", static_cast<ScopeMask>(Scope::Write), 0, 0, std::nullopt);
    bool already_revoked = false;
    CHECK(manager.revoke(revoked.id, already_revoked));

    const std::vector<MdbxKeyRecord> active_only = manager.list(false);
    CHECK_EQ(active_only.size(), 1U);
    CHECK_EQ(active_only.front().id, active.id);

    const std::vector<MdbxKeyRecord> all_records = manager.list(true);
    CHECK_EQ(all_records.size(), 2U);
}

void test_get_by_id_missing_returns_nullopt() {
    TempMdbxStore fixture;
    AuthCache cache(60000);
    FingerprintComputer fp("test-secret");
    config::AuthConfig auth_cfg;
    ApiKeyManager manager = make_manager(fixture.store(), cache, fp, auth_cfg);

    CHECK(!manager.get_by_id("missing-id").has_value());
}

} // namespace

int main() {
    test_create_generates_token_and_uuid();
    test_create_duplicate_name_throws();
    test_revoke_is_idempotent();
    test_remove_deletes_key_and_invalidates_cache();
    test_update_changes_limits_and_invalidates_cache();
    test_list_filters_revoked_records();
    test_get_by_id_missing_returns_nullopt();
    return 0;
}
