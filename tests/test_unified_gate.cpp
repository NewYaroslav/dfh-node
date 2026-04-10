/// \file test_unified_gate.cpp
/// \brief Юнит-тесты для UnifiedGate.
/// \details Проверяет HTTP auth, WS upgrade без kind, WS message и
/// anti-replay requirement policy.
///
#include "auth.hpp"
#include "config.hpp"
#include "security.hpp"
#include "test_helpers.hpp"

#include <array>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace dfh_node;

void test_http_authorize() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token1");

    entries.push_back(config::ApiKeyEntry{fingerprint, Scope::Read | Scope::Write, std::nullopt, 100, 10});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;
    UnifiedGate gate(service, limiter, ws_limiter, nullptr, 0);

    const auto result = gate.authorize_http("token1", TaskKind::Ingest);
    CHECK(std::holds_alternative<AuthContext>(result));
}

void test_ws_upgrade_no_kind() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token1");

    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Write), std::nullopt, 100, 1});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;
    UnifiedGate gate(service, limiter, ws_limiter, nullptr, 0);

    const auto result = gate.authorize_ws_upgrade("token1");
    CHECK(std::holds_alternative<AuthContext>(result));
}

void test_ws_message_scope_check() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token1");

    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Write), std::nullopt, 100, 10});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;
    UnifiedGate gate(service, limiter, ws_limiter, nullptr, 0);

    (void)gate.authorize_ws_upgrade("token1");

    const auto allowed = gate.authorize_ws_message(fingerprint, TaskKind::Ingest);
    CHECK(std::holds_alternative<AuthContext>(allowed));

    const auto forbidden = gate.authorize_ws_message(fingerprint, TaskKind::History);
    CHECK(std::holds_alternative<GateError>(forbidden));
    const auto &error = std::get<GateError>(forbidden);
    CHECK(error.code == GateErrorCode::Forbidden);
}

void test_http_rejects_when_antireplay_required_mask_contains_read() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token-read");

    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Read), std::nullopt, 100, 10});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;
    UnifiedGate gate(service, limiter, ws_limiter, nullptr, to_scope_mask(Scope::Read));

    const auto result = gate.authorize_http("token-read", TaskKind::History);
    CHECK(std::holds_alternative<GateError>(result));
    const auto &error = std::get<GateError>(result);
    CHECK(error.code == GateErrorCode::AntiReplayRequired);
}

void test_http_history_allowed_with_default_required_mask() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token-read-default");

    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Read), std::nullopt, 100, 10});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;
    UnifiedGate gate(service, limiter, ws_limiter);

    const auto result = gate.authorize_http("token-read-default", TaskKind::History);
    CHECK(std::holds_alternative<AuthContext>(result));
}

void test_ws_upgrade_connection_limited() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token-ws-limit");

    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Write), std::nullopt, 100, 1});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;
    UnifiedGate gate(service, limiter, ws_limiter, nullptr, 0);

    const auto first = gate.authorize_ws_upgrade("token-ws-limit");
    CHECK(std::holds_alternative<AuthContext>(first));

    const auto second = gate.authorize_ws_upgrade("token-ws-limit");
    CHECK(std::holds_alternative<GateError>(second));
    CHECK_EQ(std::get<GateError>(second).code, GateErrorCode::ConnectionLimited);
}

void test_http_missing_antireplay_headers_when_validator_enabled() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token-ar-http");
    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Write), std::nullopt, 100, 5});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;

    MockClock clock(1000000000000);
    config::AntiReplayConfig ar_cfg;
    ar_cfg.enabled = true;
    ar_cfg.max_skew_ms = 5000;
    ar_cfg.nonce_ttl_ms = 60000;
    ar_cfg.nonce_capacity = 100;
    NonceStore nonce_store(clock, ar_cfg.nonce_ttl_ms, ar_cfg.nonce_capacity);
    AntiReplayValidator validator(ar_cfg, clock, nonce_store);

    UnifiedGate gate(service, limiter, ws_limiter, &validator);
    const auto result = gate.authorize_http("token-ar-http", TaskKind::Ingest, nullptr);
    CHECK(std::holds_alternative<GateError>(result));
    CHECK_EQ(std::get<GateError>(result).code, GateErrorCode::MissingAntiReplayHeaders);
}

void test_ws_missing_antireplay_fields_when_validator_enabled() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string token = "token-ar-ws";
    const std::string fingerprint = computer.compute(token);
    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Write), std::nullopt, 100, 5});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;

    MockClock clock(1000000000000);
    config::AntiReplayConfig ar_cfg;
    ar_cfg.enabled = true;
    ar_cfg.max_skew_ms = 5000;
    ar_cfg.nonce_ttl_ms = 60000;
    ar_cfg.nonce_capacity = 100;
    NonceStore nonce_store(clock, ar_cfg.nonce_ttl_ms, ar_cfg.nonce_capacity);
    AntiReplayValidator validator(ar_cfg, clock, nonce_store);

    UnifiedGate gate(service, limiter, ws_limiter, &validator);

    std::array<unsigned char, 32> signing_key{};
    compute_sha256_raw(token, signing_key.data());

    const auto result =
        gate.authorize_ws_message(fingerprint, TaskKind::Ingest, signing_key.data(), signing_key.size(), nullptr);
    CHECK(std::holds_alternative<GateError>(result));
    CHECK_EQ(std::get<GateError>(result).code, GateErrorCode::MissingAntiReplayFields);
}

void test_http_antireplay_error_propagates() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string token = "token-ar-http-invalid-sig";
    const std::string fingerprint = computer.compute(token);
    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Write), std::nullopt, 100, 5});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;

    MockClock clock(1000000000000);
    config::AntiReplayConfig ar_cfg;
    ar_cfg.enabled = true;
    ar_cfg.max_skew_ms = 5000;
    ar_cfg.nonce_ttl_ms = 60000;
    ar_cfg.nonce_capacity = 100;
    NonceStore nonce_store(clock, ar_cfg.nonce_ttl_ms, ar_cfg.nonce_capacity);
    AntiReplayValidator validator(ar_cfg, clock, nonce_store);

    UnifiedGate gate(service, limiter, ws_limiter, &validator);

    HttpAntiReplayFields fields;
    fields.method = "POST";
    fields.path = "/v1/ingest";
    fields.query_params = {};
    fields.timestamp = "1000000000000";
    fields.nonce = "a1b2c3d4e5f67890";
    fields.body_hash = compute_sha256_hex("");
    fields.signature = "0000000000000000000000000000000000000000000000000000000000000000";

    const auto result = gate.authorize_http(token, TaskKind::Ingest, &fields);
    CHECK(std::holds_alternative<GateError>(result));
    CHECK_EQ(std::get<GateError>(result).code, GateErrorCode::AntiReplayFailed);
}

void test_ws_antireplay_error_propagates() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string token = "token-ar-ws-invalid";
    const std::string fingerprint = computer.compute(token);
    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Write), std::nullopt, 100, 5});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;

    MockClock clock(1000000000000);
    config::AntiReplayConfig ar_cfg;
    ar_cfg.enabled = true;
    ar_cfg.max_skew_ms = 5000;
    ar_cfg.nonce_ttl_ms = 60000;
    ar_cfg.nonce_capacity = 100;
    NonceStore nonce_store(clock, ar_cfg.nonce_ttl_ms, ar_cfg.nonce_capacity);
    AntiReplayValidator validator(ar_cfg, clock, nonce_store);

    UnifiedGate gate(service, limiter, ws_limiter, &validator);

    WsAntiReplayFields fields;
    fields.endpoint = "/ws/msgpack";
    fields.op = "ingest";
    fields.msg_id = "100";
    fields.timestamp = "1000000000000";
    fields.nonce = "abcdef1234567890";
    fields.signature = "0000000000000000000000000000000000000000000000000000000000000000";
    fields.payload_hash = compute_sha256_hex("");

    // Передаём некорректную длину ключа, чтобы валидатор anti-replay вернул ошибку.
    const auto result = gate.authorize_ws_message(fingerprint, TaskKind::Ingest, nullptr, 0, &fields);
    CHECK(std::holds_alternative<GateError>(result));
    CHECK_EQ(std::get<GateError>(result).code, GateErrorCode::AntiReplayFailed);
}

void test_auth_fail_counter_increments() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token-auth-fail");
    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Read), std::nullopt, 100, 5});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;
    UnifiedGate gate(service, limiter, ws_limiter, nullptr, 0);

    const auto unauthorized = gate.authorize_http("missing-token", TaskKind::History);
    CHECK(std::holds_alternative<GateError>(unauthorized));

    const auto forbidden = gate.authorize_http("token-auth-fail", TaskKind::Ingest);
    CHECK(std::holds_alternative<GateError>(forbidden));

    CHECK_EQ(gate.auth_fail_count(), static_cast<std::uint64_t>(2));
}

void test_rate_limit_counter_increments() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token-rate-counter");
    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Read), std::nullopt, 1, 5});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(1, 60000);
    WsConnectionLimiter ws_limiter;
    UnifiedGate gate(service, limiter, ws_limiter, nullptr, 0);

    const auto first = gate.authorize_http("token-rate-counter", TaskKind::History);
    CHECK(std::holds_alternative<AuthContext>(first));

    const auto second = gate.authorize_http("token-rate-counter", TaskKind::History);
    CHECK(std::holds_alternative<GateError>(second));
    CHECK_EQ(std::get<GateError>(second).code, GateErrorCode::RateLimited);
    CHECK_EQ(gate.rate_limit_reject_count(), static_cast<std::uint64_t>(1));
}

void test_connection_limit_counter_and_active_connections() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token-conn-counter");
    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Write), std::nullopt, 100, 5});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter(1);
    UnifiedGate gate(service, limiter, ws_limiter, nullptr, 0);

    const auto first = gate.authorize_ws_upgrade("token-conn-counter");
    CHECK(std::holds_alternative<AuthContext>(first));
    CHECK_EQ(gate.ws_active_connections(), static_cast<std::int64_t>(1));

    const auto second = gate.authorize_ws_upgrade("token-conn-counter");
    CHECK(std::holds_alternative<GateError>(second));
    CHECK_EQ(std::get<GateError>(second).code, GateErrorCode::ConnectionLimited);
    CHECK_EQ(gate.connection_limit_reject_count(), static_cast<std::uint64_t>(1));

    gate.ws_connection_closed(fingerprint);
    CHECK_EQ(gate.ws_active_connections(), static_cast<std::int64_t>(0));
}

void test_anti_replay_counter_increments() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string token = "token-ar-counter";
    const std::string fingerprint = computer.compute(token);
    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Write), std::nullopt, 100, 5});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;

    MockClock clock(1000000000000);
    config::AntiReplayConfig ar_cfg;
    ar_cfg.enabled = true;
    ar_cfg.max_skew_ms = 5000;
    ar_cfg.nonce_ttl_ms = 60000;
    ar_cfg.nonce_capacity = 100;
    NonceStore nonce_store(clock, ar_cfg.nonce_ttl_ms, ar_cfg.nonce_capacity);
    AntiReplayValidator validator(ar_cfg, clock, nonce_store);

    UnifiedGate gate(service, limiter, ws_limiter, &validator);

    const auto missing = gate.authorize_http(token, TaskKind::Ingest, nullptr);
    CHECK(std::holds_alternative<GateError>(missing));

    HttpAntiReplayFields fields;
    fields.method = "POST";
    fields.path = "/v1/ingest";
    fields.query_params = {};
    fields.timestamp = "1000000000000";
    fields.nonce = "a1b2c3d4e5f67890";
    fields.body_hash = compute_sha256_hex("");
    fields.signature = std::string(64, '0');

    const auto invalid = gate.authorize_http(token, TaskKind::Ingest, &fields);
    CHECK(std::holds_alternative<GateError>(invalid));
    CHECK_EQ(gate.anti_replay_reject_count(), static_cast<std::uint64_t>(2));
}

void test_http_unsupported_operation() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token-unsupported-http");
    entries.push_back(
        config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Read | Scope::Write), std::nullopt, 100, 5});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;
    UnifiedGate gate(service, limiter, ws_limiter, nullptr, 0);

    const auto result = gate.authorize_http("token-unsupported-http", static_cast<TaskKind>(255));
    CHECK(std::holds_alternative<GateError>(result));
    CHECK_EQ(std::get<GateError>(result).code, GateErrorCode::UnsupportedOperation);
    CHECK_EQ(gate.auth_fail_count(), static_cast<std::uint64_t>(0));
    CHECK_EQ(gate.rate_limit_reject_count(), static_cast<std::uint64_t>(0));
    CHECK_EQ(gate.anti_replay_reject_count(), static_cast<std::uint64_t>(0));
}

void test_ws_message_unsupported_operation() {
    std::vector<config::ApiKeyEntry> entries;
    FingerprintComputer computer("secret");
    const std::string fingerprint = computer.compute("token-unsupported-ws");
    entries.push_back(config::ApiKeyEntry{fingerprint, static_cast<ScopeMask>(Scope::Write), std::nullopt, 100, 5});

    ConfigApiKeyStore store(entries);
    AuthCache cache(60000);
    AuthService service(store, cache, computer);
    RateLimiter limiter(100, 1000);
    WsConnectionLimiter ws_limiter;
    UnifiedGate gate(service, limiter, ws_limiter, nullptr, 0);

    const auto upgrade = gate.authorize_ws_upgrade("token-unsupported-ws");
    CHECK(std::holds_alternative<AuthContext>(upgrade));

    const auto result = gate.authorize_ws_message(fingerprint, static_cast<TaskKind>(255));
    CHECK(std::holds_alternative<GateError>(result));
    CHECK_EQ(std::get<GateError>(result).code, GateErrorCode::UnsupportedOperation);
    CHECK_EQ(gate.auth_fail_count(), static_cast<std::uint64_t>(0));
    CHECK_EQ(gate.rate_limit_reject_count(), static_cast<std::uint64_t>(0));
    CHECK_EQ(gate.anti_replay_reject_count(), static_cast<std::uint64_t>(0));
}

int main() {
    test_http_authorize();
    test_ws_upgrade_no_kind();
    test_ws_message_scope_check();
    test_http_rejects_when_antireplay_required_mask_contains_read();
    test_http_history_allowed_with_default_required_mask();
    test_ws_upgrade_connection_limited();
    test_http_missing_antireplay_headers_when_validator_enabled();
    test_ws_missing_antireplay_fields_when_validator_enabled();
    test_http_antireplay_error_propagates();
    test_ws_antireplay_error_propagates();
    test_auth_fail_counter_increments();
    test_rate_limit_counter_increments();
    test_connection_limit_counter_and_active_connections();
    test_anti_replay_counter_increments();
    test_http_unsupported_operation();
    test_ws_message_unsupported_operation();
    return 0;
}
