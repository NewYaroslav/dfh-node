/// \file test_canonical_request.cpp
/// \brief Тесты канонизации HTTP/WS и подписи HMAC-SHA256.
/// \details Проверяет формат каноническая строка, сортировку query и верификацию подписи.
///
#include "security/canonical_request.hpp"
#include "security/sha256_utils.hpp"
#include "test_helpers.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace {

void test_canonicalize_http_basic() {
    dfh_node::HttpCanonicalInput input;
    input.method = "POST";
    input.path = "/v1/ingest";
    input.query_params = {{"symbol", "BTCUSD"}, {"exchange", "binance"}};
    input.timestamp = "1675234567890";
    input.nonce = "a1b2c3d4e5f67890";
    input.body_hash = "abc123...";

    const std::string expected = "POST\n"
                                 "/v1/ingest\n"
                                 "exchange=binance&symbol=BTCUSD\n"
                                 "1675234567890\n"
                                 "a1b2c3d4e5f67890\n"
                                 "abc123...";

    CHECK_EQ(dfh_node::canonicalize_http(input), expected);
}

void test_canonicalize_http_empty_query() {
    dfh_node::HttpCanonicalInput input;
    input.method = "GET";
    input.path = "/v1/status";
    input.timestamp = "1675234567890";
    input.nonce = "a1b2c3d4e5f67890";
    input.body_hash = dfh_node::compute_sha256_hex("");

    const std::string expected =
        std::string("GET\n") + "/v1/status\n\n1675234567890\na1b2c3d4e5f67890\n" + dfh_node::compute_sha256_hex("");
    CHECK_EQ(dfh_node::canonicalize_http(input), expected);
}

void test_canonicalize_ws() {
    dfh_node::WsCanonicalInput input;
    input.endpoint = "/ws/msgpack";
    input.op = "ingest";
    input.msg_id = "12345";
    input.timestamp = "1675234567890";
    input.nonce = "a1b2c3d4e5f67890";
    input.payload_hash = "def456...";

    const std::string expected = "/ws/msgpack\n"
                                 "ingest\n"
                                 "12345\n"
                                 "1675234567890\n"
                                 "a1b2c3d4e5f67890\n"
                                 "def456...";
    CHECK_EQ(dfh_node::canonicalize_ws(input), expected);
}

void test_compute_signature_is_deterministic() {
    std::array<unsigned char, 32> signing_key{};
    dfh_node::compute_sha256_raw("test-token", signing_key.data());

    const std::string canonical = "test canonical string";
    const std::string sig1 = dfh_node::compute_signature(canonical, signing_key.data(), signing_key.size());
    const std::string sig2 = dfh_node::compute_signature(canonical, signing_key.data(), signing_key.size());

    CHECK_EQ(sig1, sig2);
    CHECK_EQ(sig1.size(), 64u);
}

void test_verify_signature_valid() {
    std::array<unsigned char, 32> signing_key{};
    dfh_node::compute_sha256_raw("test-token", signing_key.data());

    const std::string canonical = "test canonical string";
    const std::string signature = dfh_node::compute_signature(canonical, signing_key.data(), signing_key.size());
    CHECK(dfh_node::verify_signature(canonical, signature, signing_key.data(), signing_key.size()));
}

void test_verify_signature_invalid() {
    std::array<unsigned char, 32> signing_key{};
    dfh_node::compute_sha256_raw("test-token", signing_key.data());

    const std::string canonical = "test canonical string";
    const std::string wrong_signature = "0000000000000000000000000000000000000000000000000000000000000000";
    CHECK(!dfh_node::verify_signature(canonical, wrong_signature, signing_key.data(), signing_key.size()));
}

void test_canonicalize_query_sort() {
    const std::vector<std::pair<std::string, std::string>> params = {{"b", "2"}, {"a", "1"}, {"c", "3"}};
    CHECK_EQ(dfh_node::canonicalize_query_string(params), "a=1&b=2&c=3");
}

void test_canonicalize_query_decode_encode() {
    const std::vector<std::pair<std::string, std::string>> params = {{"symbol", "BTC%2FUSD"}, {"order", "+asc"}};
    CHECK_EQ(dfh_node::canonicalize_query_string(params), "order=%20asc&symbol=BTC%2FUSD");
}

} // namespace

int main() {
    test_canonicalize_http_basic();
    test_canonicalize_http_empty_query();
    test_canonicalize_ws();
    test_compute_signature_is_deterministic();
    test_verify_signature_valid();
    test_verify_signature_invalid();
    test_canonicalize_query_sort();
    test_canonicalize_query_decode_encode();
    return 0;
}
