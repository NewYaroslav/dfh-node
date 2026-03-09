/// \file test_composite_api_key_store.cpp
/// \brief Юнит-тесты для CompositeApiKeyStore.
/// \details Проверяет приоритет bootstrap-конфига над MDBX и fallback в
/// динамическое хранилище.
///
#include "config.hpp"
#include "test_helpers.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class TempMdbxStore {
public:
    TempMdbxStore()
        : m_root(std::filesystem::temp_directory_path() /
                 ("dfh-node-composite-store-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
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

dfh_node::MdbxKeyRecord make_mdbx_record(const std::string &id, const std::string &name, const std::string &fingerprint,
                                         const dfh_node::ScopeMask scopes) {
    dfh_node::MdbxKeyRecord record;
    record.id = id;
    record.name = name;
    record.fingerprint = fingerprint;
    record.scope_mask = scopes;
    record.rps_limit = 55;
    record.ws_max_connections = 4;
    record.created_at_ms = 1000;
    record.updated_at_ms = 1000;
    return record;
}

void test_config_key_has_priority_over_mdbx() {
    const std::string fingerprint = "same-fingerprint";
    const std::vector<dfh_node::config::ApiKeyEntry> config_entries = {
        {fingerprint, dfh_node::to_scope_mask(dfh_node::Scope::Admin), std::nullopt, 100, 9},
    };
    dfh_node::ConfigApiKeyStore config_store(config_entries);

    TempMdbxStore fixture;
    fixture.store().put(
        make_mdbx_record("id-1", "mdbx-key", fingerprint, dfh_node::to_scope_mask(dfh_node::Scope::Read)));

    dfh_node::CompositeApiKeyStore composite(config_store, fixture.store());
    const auto lookup = composite.lookup(fingerprint);

    CHECK(lookup.has_value());
    CHECK_EQ(lookup->scope_mask, dfh_node::to_scope_mask(dfh_node::Scope::Admin));
    CHECK_EQ(lookup->rps_limit, 100);
}

void test_mdbx_is_used_as_fallback() {
    const std::vector<dfh_node::config::ApiKeyEntry> config_entries = {
        {"config-fp", dfh_node::to_scope_mask(dfh_node::Scope::Read), std::nullopt, 10, 1},
    };
    dfh_node::ConfigApiKeyStore config_store(config_entries);

    TempMdbxStore fixture;
    const auto record =
        make_mdbx_record("id-2", "mdbx-only", "mdbx-fp", dfh_node::to_scope_mask(dfh_node::Scope::Write));
    fixture.store().put(record);

    dfh_node::CompositeApiKeyStore composite(config_store, fixture.store());
    const auto lookup = composite.lookup(record.fingerprint);

    CHECK(lookup.has_value());
    CHECK_EQ(lookup->fingerprint, record.fingerprint);
    CHECK_EQ(lookup->scope_mask, record.scope_mask);
}

void test_config_records_are_not_part_of_mdbx_listing() {
    const std::vector<dfh_node::config::ApiKeyEntry> config_entries = {
        {"config-only-fp", dfh_node::to_scope_mask(dfh_node::Scope::Read), std::nullopt, 10, 1},
    };
    dfh_node::ConfigApiKeyStore config_store(config_entries);

    TempMdbxStore fixture;
    fixture.store().put(
        make_mdbx_record("id-3", "mdbx-visible", "mdbx-visible-fp", dfh_node::to_scope_mask(dfh_node::Scope::Write)));

    dfh_node::CompositeApiKeyStore composite(config_store, fixture.store());
    CHECK(composite.lookup("config-only-fp").has_value());

    const auto records = fixture.store().list_all();
    CHECK_EQ(records.size(), 1U);
    CHECK_EQ(records.front().name, "mdbx-visible");
}

} // namespace

int main() {
    test_config_key_has_priority_over_mdbx();
    test_mdbx_is_used_as_fallback();
    test_config_records_are_not_part_of_mdbx_listing();
    return 0;
}
