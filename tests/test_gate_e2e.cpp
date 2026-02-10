/**
 * \file test_gate_e2e.cpp
 * \brief E2E-тест полного auth/rate-limit/WS-gate сценария.
 * \details Проверяет цепочку HTTP authorize, rate limit, WS upgrade/message и
 * закрытие WS-соединения.
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

void test_gate_e2e() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string write_fingerprint = computer.compute("write-token");

    entries.push_back(config::ApiKeyEntry{write_fingerprint,
                                          static_cast<ScopeMask>(Scope::Write),
                                          std::nullopt, 2, 1});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(2, 1000);
    WsConnectionLimiter ws_limiter;
    UnifiedGate gate(service, limiter, ws_limiter, nullptr, 0);

    const auto http_ok = gate.authorize_http("write-token", TaskKind::Ingest);
    CHECK(std::holds_alternative<AuthContext>(http_ok));

    (void)gate.authorize_http("write-token", TaskKind::Ingest);
    const auto http_limited =
        gate.authorize_http("write-token", TaskKind::Ingest);
    CHECK(std::holds_alternative<GateError>(http_limited));
    CHECK(std::get<GateError>(http_limited).code == GateErrorCode::RateLimited);

    // Для WS-сценария берём отдельный лимитер, чтобы не зависеть от уже
    // исчерпанного HTTP-окна и проверить именно WS upgrade/message цепочку.
    RateLimiter ws_limiter_rps(100, 1000);
    UnifiedGate ws_gate(service, ws_limiter_rps, ws_limiter, nullptr, 0);

    const auto upgrade = ws_gate.authorize_ws_upgrade("write-token");
    CHECK(std::holds_alternative<AuthContext>(upgrade));
    const auto &ctx = std::get<AuthContext>(upgrade);

    const auto ws_msg_ok =
        ws_gate.authorize_ws_message(ctx.fingerprint, TaskKind::Ingest);
    CHECK(std::holds_alternative<AuthContext>(ws_msg_ok));

    const auto ws_msg_forbidden =
        ws_gate.authorize_ws_message(ctx.fingerprint, TaskKind::History);
    CHECK(std::holds_alternative<GateError>(ws_msg_forbidden));
    CHECK(std::get<GateError>(ws_msg_forbidden).code == GateErrorCode::Forbidden);

    ws_gate.ws_connection_closed(ctx.fingerprint);
}

int main() {
    test_gate_e2e();
    return 0;
}
