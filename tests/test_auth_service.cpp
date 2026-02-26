/// \file test_auth_service.cpp
/// \brief Юнит-тесты для AuthService.
/// \details Проверяет три публичных метода и обработку неизвестного TaskKind.
///
#include "auth_service.hpp"
#include "config.hpp"
#include "config_api_key_store.hpp"
#include "test_helpers.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace dfh_node;

void test_authenticate_token_no_scope_check() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token1");

    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Write), std::nullopt, 100, 10});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);

    const auto result = service.authenticate_token("token1");
    CHECK(std::holds_alternative<AuthContext>(result));
}

void test_authorize_with_scope() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token1");

    entries.push_back(config::ApiKeyEntry{fingerprint, Scope::Read | Scope::Write, std::nullopt, 100, 10});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);

    const auto result = service.authorize("token1", TaskKind::Ingest);
    CHECK(std::holds_alternative<AuthContext>(result));
}

void test_authorize_insufficient_scope() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token1");

    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Read), std::nullopt, 100, 10});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);

    const auto result = service.authorize("token1", TaskKind::Ingest);
    CHECK(std::holds_alternative<GateError>(result));
    const auto &error = std::get<GateError>(result);
    CHECK(error.code == GateErrorCode::Forbidden);
}

void test_authorize_fingerprint() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token1");

    entries.push_back(config::ApiKeyEntry{fingerprint, Scope::Read | Scope::Write, std::nullopt, 100, 10});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);

    (void)service.authenticate_token("token1");

    const auto result = service.authorize_fingerprint(fingerprint, TaskKind::History);
    CHECK(std::holds_alternative<AuthContext>(result));
}

void test_unknown_task_kind() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token1");

    entries.push_back(config::ApiKeyEntry{fingerprint, Scope::Read | Scope::Write, std::nullopt, 100, 10});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);

    const auto result = service.authorize("token1", static_cast<TaskKind>(99));
    CHECK(std::holds_alternative<GateError>(result));
    const auto &error = std::get<GateError>(result);
    CHECK(error.code == GateErrorCode::UnsupportedOperation);
}

void test_authenticate_invalid_token() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    entries.push_back(
        config::ApiKeyEntry{computer.compute("token1"), Scope::Read | Scope::Write, std::nullopt, 100, 10});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);

    const auto result = service.authenticate_token("unknown-token");
    CHECK(std::holds_alternative<GateError>(result));
    const auto &error = std::get<GateError>(result);
    CHECK(error.code == GateErrorCode::Unauthorized);
}

void test_authenticate_expired_token() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("expired-token");

    // Значение 0 гарантированно меньше текущего epoch времени.
    entries.push_back(
        config::ApiKeyEntry{fingerprint, Scope::Read | Scope::Write, static_cast<std::int64_t>(0), 100, 10});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);

    const auto result = service.authenticate_token("expired-token");
    CHECK(std::holds_alternative<GateError>(result));
    const auto &error = std::get<GateError>(result);
    CHECK(error.code == GateErrorCode::Unauthorized);
}

int main() {
    test_authenticate_token_no_scope_check();
    test_authorize_with_scope();
    test_authorize_insufficient_scope();
    test_authorize_fingerprint();
    test_unknown_task_kind();
    test_authenticate_invalid_token();
    test_authenticate_expired_token();
    return 0;
}
