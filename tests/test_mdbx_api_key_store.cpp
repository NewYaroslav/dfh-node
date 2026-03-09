/// \file test_mdbx_api_key_store.cpp
/// \brief Юнит-тесты для MdbxApiKeyStore.
/// \details Проверяет CRUD-операции, фильтрацию revoked/expired и готовность
/// MDBX-store для readiness.
///
#include "config.hpp"
#include "test_helpers.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

class TempMdbxStore {
public:
    TempMdbxStore()
        : m_root(
              std::filesystem::temp_directory_path() /
              ("dfh-node-mdbx-store-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
          m_env_path(m_root / "keys.mdbx"), m_store(m_env_path.string()) {
        std::filesystem::create_directories(m_root);
        m_store.open();
    }

    dfh_node::MdbxApiKeyStore &store() { return m_store; }

private:
    std::filesystem::path m_root;
    std::filesystem::path m_env_path;
    dfh_node::MdbxApiKeyStore m_store;
};

dfh_node::MdbxKeyRecord make_record(const std::string &id, const std::string &name, const std::string &fingerprint) {
    dfh_node::MdbxKeyRecord record;
    record.id = id;
    record.name = name;
    record.fingerprint = fingerprint;
    record.scope_mask =
        dfh_node::to_scope_mask(dfh_node::Scope::Read) | dfh_node::to_scope_mask(dfh_node::Scope::Write);
    record.rps_limit = 123;
    record.ws_max_connections = 7;
    record.created_at_ms = 1000;
    record.updated_at_ms = 2000;
    return record;
}

void test_put_and_lookup_returns_api_key_record() {
    TempMdbxStore fixture;
    const auto record = make_record("id-1", "key-1", "fp-1");
    fixture.store().put(record);

    const auto lookup = fixture.store().lookup(record.fingerprint);
    CHECK(lookup.has_value());
    CHECK_EQ(lookup->fingerprint, record.fingerprint);
    CHECK_EQ(lookup->scope_mask, record.scope_mask);
    CHECK_EQ(lookup->rps_limit, record.rps_limit);
    CHECK_EQ(lookup->ws_max_connections, record.ws_max_connections);
}

void test_lookup_hides_revoked_and_expired_records() {
    TempMdbxStore fixture;

    auto revoked = make_record("id-2", "revoked", "fp-2");
    revoked.revoked = true;
    fixture.store().put(revoked);
    CHECK(!fixture.store().lookup(revoked.fingerprint).has_value());

    auto expired = make_record("id-3", "expired", "fp-3");
    expired.expires_at_ms = 1;
    fixture.store().put(expired);
    CHECK(!fixture.store().lookup(expired.fingerprint).has_value());
}

void test_get_by_id_and_name_and_remove() {
    TempMdbxStore fixture;
    const auto record = make_record("id-4", "lookup-me", "fp-4");
    fixture.store().put(record);

    const auto by_id = fixture.store().get_by_id(record.id);
    CHECK(by_id.has_value());
    CHECK_EQ(by_id->name, record.name);

    const auto by_name = fixture.store().get_by_name(record.name);
    CHECK(by_name.has_value());
    CHECK_EQ(by_name->id, record.id);

    CHECK(fixture.store().remove(record.id));
    CHECK(!fixture.store().lookup(record.fingerprint).has_value());
    CHECK(!fixture.store().get_by_id(record.id).has_value());
    CHECK(!fixture.store().get_by_name(record.name).has_value());
    CHECK(!fixture.store().remove(record.id));
}

void test_list_all_includes_revoked_and_health_is_true() {
    TempMdbxStore fixture;

    auto active = make_record("id-5", "active", "fp-5");
    auto revoked = make_record("id-6", "revoked-list", "fp-6");
    revoked.revoked = true;

    fixture.store().put(active);
    fixture.store().put(revoked);

    const auto records = fixture.store().list_all();
    CHECK_EQ(records.size(), 2U);
    CHECK(fixture.store().is_healthy());
}

void test_duplicate_name_throws() {
    TempMdbxStore fixture;

    fixture.store().put(make_record("id-7", "dup-name", "fp-7"));

    bool thrown = false;
    try {
        fixture.store().put(make_record("id-8", "dup-name", "fp-8"));
    } catch (const std::runtime_error &error) {
        thrown = std::string(error.what()) == "duplicate name";
    }

    CHECK(thrown);
}

void test_duplicate_fingerprint_throws() {
    TempMdbxStore fixture;

    fixture.store().put(make_record("id-9", "name-9", "dup-fp"));

    bool thrown = false;
    try {
        fixture.store().put(make_record("id-10", "name-10", "dup-fp"));
    } catch (const std::runtime_error &error) {
        thrown = std::string(error.what()) == "duplicate fingerprint";
    }

    CHECK(thrown);
}

} // namespace

int main() {
    test_put_and_lookup_returns_api_key_record();
    test_lookup_hides_revoked_and_expired_records();
    test_get_by_id_and_name_and_remove();
    test_list_all_includes_revoked_and_health_is_true();
    test_duplicate_name_throws();
    test_duplicate_fingerprint_throws();
    return 0;
}
