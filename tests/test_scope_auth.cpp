/// \file test_scope_auth.cpp
/// \brief Юнит-тесты inline-утилит scope и auth-контракта.
/// \details Проверяет parse_scope, required_scope и has_scope.
///
#include "auth.hpp"
#include "test_helpers.hpp"

using namespace dfh_node;

namespace {

void test_parse_scope() {
    CHECK(parse_scope("read").has_value());
    CHECK(parse_scope("write").has_value());
    CHECK(parse_scope("admin").has_value());
    CHECK(parse_scope("sync").has_value());
    CHECK(!parse_scope("READ").has_value());
    CHECK(!parse_scope("unknown").has_value());
}

void test_required_scope_mapping() {
    const auto ingest_scope = required_scope(TaskKind::Ingest);
    CHECK(ingest_scope.has_value());
    CHECK_EQ(*ingest_scope, Scope::Write);

    const auto history_scope = required_scope(TaskKind::History);
    CHECK(history_scope.has_value());
    CHECK_EQ(*history_scope, Scope::Read);

    const auto sync_scope = required_scope(TaskKind::Sync);
    CHECK(sync_scope.has_value());
    CHECK_EQ(*sync_scope, Scope::Sync);

    const auto unsupported = required_scope(static_cast<TaskKind>(255));
    CHECK(!unsupported.has_value());
}

void test_has_scope_checks_mask() {
    const ScopeMask mask = to_scope_mask(Scope::Read) | to_scope_mask(Scope::Write);
    CHECK(has_scope(mask, Scope::Read));
    CHECK(has_scope(mask, Scope::Write));
    CHECK(!has_scope(mask, Scope::Admin));
    CHECK(!has_scope(mask, Scope::Sync));
}

} // namespace

int main() {
    test_parse_scope();
    test_required_scope_mapping();
    test_has_scope_checks_mask();
    return 0;
}
