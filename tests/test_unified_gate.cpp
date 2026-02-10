/**
 * \file test_unified_gate.cpp
 * \brief Unit-тесты для UnifiedGate.
 * \details Проверяет HTTP auth, WS upgrade без kind и WS message с проверкой
 * scope.
 */
#include "config.hpp"
#include "config_api_key_store.hpp"
#include "test_helpers.hpp"
#include "unified_gate.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace dfh_node;

void test_http_authorize() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token1");

    entries.push_back(config::ApiKeyEntry{
        fingerprint, Scope::Read | Scope::Write, std::nullopt, 100, 10});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;
    UnifiedGate gate(service, limiter, ws_limiter);

    const auto result = gate.authorize_http("token1", TaskKind::Ingest);
    CHECK(std::holds_alternative<AuthContext>(result));
}

void test_ws_upgrade_no_kind() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token1");

    entries.push_back(config::ApiKeyEntry{fingerprint,
                                          static_cast<ScopeMask>(Scope::Write),
                                          std::nullopt, 100, 1});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;
    UnifiedGate gate(service, limiter, ws_limiter);

    const auto result = gate.authorize_ws_upgrade("token1");
    CHECK(std::holds_alternative<AuthContext>(result));
}

void test_ws_message_scope_check() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token1");

    entries.push_back(config::ApiKeyEntry{fingerprint,
                                          static_cast<ScopeMask>(Scope::Write),
                                          std::nullopt, 100, 10});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;
    UnifiedGate gate(service, limiter, ws_limiter);

    (void)gate.authorize_ws_upgrade("token1");

    const auto allowed =
        gate.authorize_ws_message(fingerprint, TaskKind::Ingest);
    CHECK(std::holds_alternative<AuthContext>(allowed));

    const auto forbidden =
        gate.authorize_ws_message(fingerprint, TaskKind::History);
    CHECK(std::holds_alternative<GateError>(forbidden));
    const auto &error = std::get<GateError>(forbidden);
    CHECK(error.code == GateErrorCode::Forbidden);
}

int main() {
    test_http_authorize();
    test_ws_upgrade_no_kind();
    test_ws_message_scope_check();
    return 0;
}
