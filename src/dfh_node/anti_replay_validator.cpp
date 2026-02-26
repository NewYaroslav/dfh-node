/// \file anti_replay_validator.cpp
/// \brief Реализация anti-replay валидации для HTTP/WS.
/// \details Проверки выполняются строго в порядке parse -> skew -> signature -> nonce.
///
#include "anti_replay_validator.hpp"

#include "logging.hpp"

#include <LogIt.hpp>

#include <cstdint>
#include <string>

namespace dfh_node {
namespace {

/// Проверяет, что строка состоит только из цифр.
bool is_decimal_string(const std::string &value) {
    if (value.empty()) {
        return false;
    }

    for (const char ch : value) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    return true;
}

/// Проверяет hex в нижнем регистре строку ожидаемой длины.
bool is_hex_lower_string(const std::string &value, const std::size_t expected_len) {
    if (value.size() != expected_len) {
        return false;
    }

    for (const char ch : value) {
        const bool is_digit = ch >= '0' && ch <= '9';
        const bool is_lower_hex = ch >= 'a' && ch <= 'f';
        if (!is_digit && !is_lower_hex) {
            return false;
        }
    }
    return true;
}

} // namespace

AntiReplayValidator::AntiReplayValidator(const config::AntiReplayConfig &config, IClock &system_clock,
                                         NonceStore &nonce_store)
    : m_config(config), m_system_clock(system_clock), m_nonce_store(nonce_store) {}

GateResult AntiReplayValidator::validate_http(const std::string &fingerprint, const unsigned char *signing_key,
                                              const std::size_t key_len, const HttpCanonicalInput &input,
                                              const std::string &signature) {
    const std::string canonical = canonicalize_http(input);
    return validate_common(fingerprint, signing_key, key_len, canonical, input.timestamp, input.nonce, signature);
}

GateResult AntiReplayValidator::validate_ws(const std::string &fingerprint, const unsigned char *signing_key,
                                            const std::size_t key_len, const WsCanonicalInput &input,
                                            const std::string &signature) {
    const std::string canonical = canonicalize_ws(input);
    return validate_common(fingerprint, signing_key, key_len, canonical, input.timestamp, input.nonce, signature);
}

GateResult AntiReplayValidator::validate_common(const std::string &fingerprint, const unsigned char *signing_key,
                                                const std::size_t key_len, const std::string &canonical,
                                                const std::string &timestamp, const std::string &nonce,
                                                const std::string &signature) {
    if (key_len != 32u) {
        DFH_WARN("Anti-replay: invalid signing key length");
        return GateError{GateErrorCode::AntiReplayFailed, "Invalid signing_key length"};
    }

    if (timestamp.size() != 13u || !is_decimal_string(timestamp)) {
        DFH_WARN("Anti-replay: invalid timestamp format");
        return GateError{GateErrorCode::AntiReplayFailed, "Invalid timestamp format"};
    }

    if (!is_hex_lower_string(nonce, 16u)) {
        DFH_WARN("Anti-replay: invalid nonce format");
        return GateError{GateErrorCode::AntiReplayFailed, "Invalid nonce format"};
    }

    if (!is_hex_lower_string(signature, 64u)) {
        DFH_WARN("Anti-replay: invalid signature format");
        return GateError{GateErrorCode::AntiReplayFailed, "Invalid signature format"};
    }

    const std::int64_t now_ms = static_cast<std::int64_t>(m_system_clock.now_ms());
    const std::int64_t request_ts = std::stoll(timestamp);
    const std::int64_t delta = (now_ms >= request_ts) ? (now_ms - request_ts) : (request_ts - now_ms);
    if (delta > m_config.max_skew_ms) {
        DFH_WARN("Anti-replay: timestamp skew too large");
        return GateError{GateErrorCode::AntiReplayFailed, "Timestamp skew too large"};
    }

    if (!verify_signature(canonical, signature, signing_key, key_len)) {
        DFH_WARN("Anti-replay: invalid signature");
        return GateError{GateErrorCode::AntiReplayFailed, "Invalid signature"};
    }

    if (!m_nonce_store.check_and_record(fingerprint, nonce, now_ms)) {
        DFH_WARN("Anti-replay: nonce reuse detected");
        return GateError{GateErrorCode::AntiReplayFailed, "Nonce reuse (replay detected)"};
    }

    return std::monostate{};
}

} // namespace dfh_node
