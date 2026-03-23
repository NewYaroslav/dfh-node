/// \file test_sha256_utils.cpp
/// \brief Тесты утилит SHA-256.
/// \details Проверяет детерминированность hash и базовую верификацию.
///
#include "security.hpp"
#include "test_helpers.hpp"

#include <string>
#include <vector>

namespace {

void test_compute_sha256_hex_known_value() {
    const std::string input = "hello world";
    const std::string expected = "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9";
    CHECK_EQ(dfh_node::compute_sha256_hex(input), expected);
}

void test_compute_sha256_hex_empty_value() {
    const std::string expected = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    CHECK_EQ(dfh_node::compute_sha256_hex(""), expected);
}

void test_verify_sha256_match() {
    const std::string input = "test data";
    const std::string hash = dfh_node::compute_sha256_hex(input);
    CHECK(dfh_node::verify_sha256(input, hash));
}

void test_verify_sha256_mismatch() {
    const std::string input = "test data";
    const std::string wrong_hash = "0000000000000000000000000000000000000000000000000000000000000000";
    CHECK(!dfh_node::verify_sha256(input, wrong_hash));
}

void test_verify_sha256_invalid_length() { CHECK(!dfh_node::verify_sha256("test data", "12345")); }

void test_hex_to_bytes_valid_lowercase() {
    const std::vector<std::uint8_t> bytes = dfh_node::hex_to_bytes("00ff10");
    CHECK_EQ(bytes.size(), 3U);
    CHECK_EQ(bytes[0], static_cast<std::uint8_t>(0x00));
    CHECK_EQ(bytes[1], static_cast<std::uint8_t>(0xff));
    CHECK_EQ(bytes[2], static_cast<std::uint8_t>(0x10));
}

void test_hex_to_bytes_valid_uppercase() {
    const std::vector<std::uint8_t> bytes = dfh_node::hex_to_bytes("ABCD");
    CHECK_EQ(bytes.size(), 2U);
    CHECK_EQ(bytes[0], static_cast<std::uint8_t>(0xab));
    CHECK_EQ(bytes[1], static_cast<std::uint8_t>(0xcd));
}

void test_hex_to_bytes_invalid_input() {
    CHECK(dfh_node::hex_to_bytes("0").empty());
    CHECK(dfh_node::hex_to_bytes("zz").empty());
}

} // namespace

int main() {
    test_compute_sha256_hex_known_value();
    test_compute_sha256_hex_empty_value();
    test_verify_sha256_match();
    test_verify_sha256_mismatch();
    test_verify_sha256_invalid_length();
    test_hex_to_bytes_valid_lowercase();
    test_hex_to_bytes_valid_uppercase();
    test_hex_to_bytes_invalid_input();
    return 0;
}
