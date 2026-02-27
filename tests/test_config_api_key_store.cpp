/// \file test_config_api_key_store.cpp
/// \brief Юнит-тесты для ConfigApiKeyStore.
/// \details Проверяет поиск существующего/несуществующего ключа и поведение при дубликатах fingerprint.
///
#include "config.hpp"
#include "test_helpers.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace dfh_node;

namespace {

void test_lookup_existing_record() {
    std::vector<config::ApiKeyEntry> entries{
        config::ApiKeyEntry{"fp-1", Scope::Read | Scope::Write, std::nullopt, 123, 7},
    };
    ConfigApiKeyStore store(entries);

    const auto record = store.lookup("fp-1");
    CHECK(record.has_value());
    CHECK_EQ(record->fingerprint, "fp-1");
    CHECK_EQ(record->scope_mask, Scope::Read | Scope::Write);
    CHECK_EQ(record->rps_limit, 123);
    CHECK_EQ(record->ws_max_connections, 7);
}

void test_lookup_missing_record() {
    std::vector<config::ApiKeyEntry> entries{
        config::ApiKeyEntry{"fp-1", static_cast<ScopeMask>(Scope::Read), std::nullopt, 100, 5},
    };
    ConfigApiKeyStore store(entries);

    const auto record = store.lookup("fp-unknown");
    CHECK(!record.has_value());
}

void test_duplicate_fingerprint_last_entry_wins() {
    std::vector<config::ApiKeyEntry> entries{
        config::ApiKeyEntry{"fp-dup", static_cast<ScopeMask>(Scope::Read), std::nullopt, 10, 1},
        config::ApiKeyEntry{"fp-dup", static_cast<ScopeMask>(Scope::Write), std::nullopt, 99, 3},
    };
    ConfigApiKeyStore store(entries);

    const auto record = store.lookup("fp-dup");
    CHECK(record.has_value());
    CHECK_EQ(record->scope_mask, static_cast<ScopeMask>(Scope::Write));
    CHECK_EQ(record->rps_limit, 99);
    CHECK_EQ(record->ws_max_connections, 3);
}

} // namespace

int main() {
    test_lookup_existing_record();
    test_lookup_missing_record();
    test_duplicate_fingerprint_last_entry_wins();
    return 0;
}
