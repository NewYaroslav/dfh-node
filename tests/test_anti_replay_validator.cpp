/// \file test_anti_replay_validator.cpp
/// \brief Тесты anti-replay валидации для HTTP и WS.
/// \details Проверяет рассинхрон времени, подпись, повтор nonce и влияние payload_hash.
///
#include "anti_replay_validator.hpp"
#include "nonce_store.hpp"
#include "sha256_utils.hpp"
#include "test_helpers.hpp"

#include <array>
#include <variant>

namespace {

dfh_node::config::AntiReplayConfig make_config() {
    dfh_node::config::AntiReplayConfig cfg;
    cfg.enabled = true;
    cfg.max_skew_ms = 5000;
    cfg.nonce_ttl_ms = 60000;
    cfg.nonce_capacity = 100;
    return cfg;
}

std::array<unsigned char, 32> make_signing_key() {
    std::array<unsigned char, 32> signing_key{};
    dfh_node::compute_sha256_raw("test-token", signing_key.data());
    return signing_key;
}

dfh_node::HttpCanonicalInput make_http_input(const std::string &timestamp, const std::string &nonce) {
    dfh_node::HttpCanonicalInput input;
    input.method = "POST";
    input.path = "/v1/ingest";
    input.timestamp = timestamp;
    input.nonce = nonce;
    input.body_hash = dfh_node::compute_sha256_hex("");
    return input;
}

std::string make_signature(const dfh_node::HttpCanonicalInput &input, const std::array<unsigned char, 32> &key) {
    const std::string canonical = dfh_node::canonicalize_http(input);
    return dfh_node::compute_signature(canonical, key.data(), key.size());
}

dfh_node::WsCanonicalInput make_ws_input(const std::string &timestamp, const std::string &nonce,
                                         const std::string &payload_hash) {
    dfh_node::WsCanonicalInput input;
    input.endpoint = "/ws/msgpack";
    input.op = "ingest";
    input.msg_id = "42";
    input.timestamp = timestamp;
    input.nonce = nonce;
    input.payload_hash = payload_hash;
    return input;
}

std::string make_signature(const dfh_node::WsCanonicalInput &input, const std::array<unsigned char, 32> &key) {
    const std::string canonical = dfh_node::canonicalize_ws(input);
    return dfh_node::compute_signature(canonical, key.data(), key.size());
}

void test_valid_request() {
    MockClock clock(1000000000000);
    dfh_node::NonceStore store(clock, 60000, 100);
    const auto cfg = make_config();
    dfh_node::AntiReplayValidator validator(cfg, clock, store);
    const auto signing_key = make_signing_key();
    const auto input = make_http_input("1000000000000", "a1b2c3d4e5f67890");
    const std::string signature = make_signature(input, signing_key);

    const auto result = validator.validate_http("fp1", signing_key.data(), signing_key.size(), input, signature);
    CHECK(std::holds_alternative<std::monostate>(result));
}

void test_timestamp_skew_too_large_past() {
    MockClock clock(1000000000000);
    dfh_node::NonceStore store(clock, 60000, 100);
    const auto cfg = make_config();
    dfh_node::AntiReplayValidator validator(cfg, clock, store);
    const auto signing_key = make_signing_key();
    const auto input = make_http_input("999999990000", "a1b2c3d4e5f67890");
    const std::string signature = make_signature(input, signing_key);

    const auto result = validator.validate_http("fp1", signing_key.data(), signing_key.size(), input, signature);
    CHECK(std::holds_alternative<dfh_node::GateError>(result));
    CHECK_EQ(std::get<dfh_node::GateError>(result).code, dfh_node::GateErrorCode::AntiReplayFailed);
}

void test_timestamp_skew_too_large_future() {
    MockClock clock(1000000000000);
    dfh_node::NonceStore store(clock, 60000, 100);
    const auto cfg = make_config();
    dfh_node::AntiReplayValidator validator(cfg, clock, store);
    const auto signing_key = make_signing_key();
    const auto input = make_http_input("1000000010000", "a1b2c3d4e5f67890");
    const std::string signature = make_signature(input, signing_key);

    const auto result = validator.validate_http("fp1", signing_key.data(), signing_key.size(), input, signature);
    CHECK(std::holds_alternative<dfh_node::GateError>(result));
    CHECK_EQ(std::get<dfh_node::GateError>(result).code, dfh_node::GateErrorCode::AntiReplayFailed);
}

void test_timestamp_future_within_skew_allowed() {
    MockClock clock(1000000000000);
    dfh_node::NonceStore store(clock, 60000, 100);
    const auto cfg = make_config();
    dfh_node::AntiReplayValidator validator(cfg, clock, store);
    const auto signing_key = make_signing_key();
    const auto input = make_http_input("1000000003000", "a1b2c3d4e5f67890");
    const std::string signature = make_signature(input, signing_key);

    const auto result = validator.validate_http("fp1", signing_key.data(), signing_key.size(), input, signature);
    CHECK(std::holds_alternative<std::monostate>(result));
}

void test_nonce_reuse() {
    MockClock clock(1000000000000);
    dfh_node::NonceStore store(clock, 60000, 100);
    const auto cfg = make_config();
    dfh_node::AntiReplayValidator validator(cfg, clock, store);
    const auto signing_key = make_signing_key();
    const auto input = make_http_input("1000000000000", "a1b2c3d4e5f67890");
    const std::string signature = make_signature(input, signing_key);

    const auto first = validator.validate_http("fp1", signing_key.data(), signing_key.size(), input, signature);
    CHECK(std::holds_alternative<std::monostate>(first));

    const auto second = validator.validate_http("fp1", signing_key.data(), signing_key.size(), input, signature);
    CHECK(std::holds_alternative<dfh_node::GateError>(second));
    CHECK_EQ(std::get<dfh_node::GateError>(second).code, dfh_node::GateErrorCode::AntiReplayFailed);
}

void test_invalid_signature_does_not_touch_nonce_store() {
    MockClock clock(1000000000000);
    dfh_node::NonceStore store(clock, 60000, 100);
    const auto cfg = make_config();
    dfh_node::AntiReplayValidator validator(cfg, clock, store);
    const auto signing_key = make_signing_key();
    const auto input = make_http_input("1000000000000", "a1b2c3d4e5f67890");
    const std::string wrong_signature = "0000000000000000000000000000000000000000000000000000000000000000";

    const auto result = validator.validate_http("fp1", signing_key.data(), signing_key.size(), input, wrong_signature);
    CHECK(std::holds_alternative<dfh_node::GateError>(result));
    CHECK_EQ(std::get<dfh_node::GateError>(result).code, dfh_node::GateErrorCode::AntiReplayFailed);
    CHECK_EQ(store.size(), 0u);
}

void test_ws_valid_request() {
    MockClock clock(1000000000000);
    dfh_node::NonceStore store(clock, 60000, 100);
    const auto cfg = make_config();
    dfh_node::AntiReplayValidator validator(cfg, clock, store);
    const auto signing_key = make_signing_key();
    const auto payload_hash = dfh_node::compute_sha256_hex("dfhbin-payload");
    const auto input = make_ws_input("1000000000000", "abcdef1234567890", payload_hash);
    const std::string signature = make_signature(input, signing_key);

    const auto result = validator.validate_ws("fp1", signing_key.data(), signing_key.size(), input, signature);
    CHECK(std::holds_alternative<std::monostate>(result));
}

void test_ws_payload_hash_mismatch() {
    MockClock clock(1000000000000);
    dfh_node::NonceStore store(clock, 60000, 100);
    const auto cfg = make_config();
    dfh_node::AntiReplayValidator validator(cfg, clock, store);
    const auto signing_key = make_signing_key();

    const auto input_signed =
        make_ws_input("1000000000000", "abcdef1234567891", dfh_node::compute_sha256_hex("payload-original"));
    const std::string signature = make_signature(input_signed, signing_key);

    // Эмулируем подмену payload_hash после подписания control-сообщения.
    auto input_tampered = input_signed;
    input_tampered.payload_hash = dfh_node::compute_sha256_hex("payload-tampered");

    const auto result =
        validator.validate_ws("fp1", signing_key.data(), signing_key.size(), input_tampered, signature);
    CHECK(std::holds_alternative<dfh_node::GateError>(result));
    CHECK_EQ(std::get<dfh_node::GateError>(result).code, dfh_node::GateErrorCode::AntiReplayFailed);
}

} // namespace

int main() {
    test_valid_request();
    test_timestamp_skew_too_large_past();
    test_timestamp_skew_too_large_future();
    test_timestamp_future_within_skew_allowed();
    test_nonce_reuse();
    test_invalid_signature_does_not_touch_nonce_store();
    test_ws_valid_request();
    test_ws_payload_hash_mismatch();
    return 0;
}
