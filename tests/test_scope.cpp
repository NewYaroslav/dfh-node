/// \file test_scope.cpp
/// \brief Юнит-тесты для Scope и функций авторизации.
/// \details Проверяет комбинирование битмаски, Admin override, парсинг и маппинг
/// TaskKind.
///
#include "auth/scope.hpp"
#include "test_helpers.hpp"

using namespace dfh_node;

void test_scope_operators() {
    ScopeMask mask = Scope::Read | Scope::Write | Scope::Sync;
    CHECK(has_scope(mask, Scope::Read));
    CHECK(has_scope(mask, Scope::Write));
    CHECK(has_scope(mask, Scope::Sync));
    CHECK(!has_scope(mask, Scope::Admin));
}

void test_has_scope() {
    ScopeMask read_write = Scope::Read | Scope::Write;
    CHECK(has_scope(read_write, Scope::Read));
    CHECK(has_scope(read_write, Scope::Write));
    CHECK(!has_scope(read_write, Scope::Sync));
}

void test_admin_override() {
    ScopeMask admin_only = static_cast<ScopeMask>(Scope::Admin);
    CHECK(has_scope(admin_only, Scope::Read));
    CHECK(has_scope(admin_only, Scope::Write));
    CHECK(has_scope(admin_only, Scope::Sync));
}

void test_parse_scope() {
    CHECK(parse_scope("read") == Scope::Read);
    CHECK(parse_scope("write") == Scope::Write);
    CHECK(parse_scope("admin") == Scope::Admin);
    CHECK(parse_scope("sync") == Scope::Sync);
    CHECK(!parse_scope("invalid").has_value());
}

void test_required_scope() {
    CHECK(required_scope(TaskKind::Ingest) == Scope::Write);
    CHECK(required_scope(TaskKind::History) == Scope::Read);
    CHECK(!required_scope(static_cast<TaskKind>(99)).has_value());
}

int main() {
    test_scope_operators();
    test_has_scope();
    test_admin_override();
    test_parse_scope();
    test_required_scope();
    return 0;
}
