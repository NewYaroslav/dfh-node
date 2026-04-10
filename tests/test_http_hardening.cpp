/// \file test_http_hardening.cpp
/// \brief Hardening-тесты HTTP transport.
/// \details Фиксирует негативные auth/scope, corpus-style DTO и anti-replay
/// сценарии поверх реального `HttpServer`.
///
#include "transport_test_utils.hpp"

#include <future>

namespace {

using test_support::all_scopes_mask;
using test_support::HttpHeaders;
using test_support::make_http_anti_replay_headers;
using test_support::make_http_base_config;
using test_support::make_ingest_body;
using test_support::RunningHttpNode;

HttpHeaders json_headers() {
    HttpHeaders headers;
    headers.emplace("Content-Type", "application/json");
    return headers;
}

void expect_error_response(const test_support::HttpResponse &response, const int status, const char *error_code) {
    if (response.status != status) {
        std::cerr << "unexpected status: actual=" << response.status << " expected=" << status
                  << " body=" << response.body << "\n";
    }
    CHECK_EQ(response.status, status);
    CHECK_EQ(nlohmann::json::parse(response.body).at("error").get<std::string>(), std::string(error_code));
}

void test_auth_and_scope() {
    auto cfg = make_http_base_config();
    RunningHttpNode node(std::move(cfg), "token-http-auth", all_scopes_mask());

    const auto no_auth = node.request("GET", "/v1/status", "", false);
    expect_error_response(no_auth, 401, "unauthorized");

    HttpHeaders bad_headers = json_headers();
    bad_headers.emplace("Authorization", "Bearer invalid-token");
    const auto bad_token = node.request("POST", "/v1/ingest", make_ingest_body(), false, 5, bad_headers);
    expect_error_response(bad_token, 401, "unauthorized");

    cfg = make_http_base_config();
    RunningHttpNode read_only(std::move(cfg), "token-http-read-only", dfh_node::to_scope_mask(dfh_node::Scope::Read));
    const auto forbidden = read_only.request("POST", "/v1/ingest", make_ingest_body(), true, 5, json_headers());
    expect_error_response(forbidden, 403, "forbidden");
}

void test_content_type_and_dto_corpus() {
    auto cfg = make_http_base_config();
    cfg.http.history_max_range_ms = 1000;
    RunningHttpNode node(std::move(cfg), "token-http-dto");

    const auto no_json_content_type = node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, HttpHeaders{});
    CHECK_EQ(no_json_content_type.status, 200);

    const auto raw_bytes = node.request("POST", "/v1/ingest", std::string("\x00\x01\x02", 3), true, 5, json_headers());
    expect_error_response(raw_bytes, 400, "invalid_json");

    const auto truncated = node.request("POST", "/v1/ingest", R"([{"key":)", true, 5, json_headers());
    expect_error_response(truncated, 400, "invalid_json");

    const auto empty_array = node.request("POST", "/v1/ingest", "[]", true, 5, json_headers());
    expect_error_response(empty_array, 400, "invalid_query_param");

    const auto invalid_base64 = node.request(
        "POST", "/v1/ingest", make_ingest_body("binance", "BTCUSDT", "spot", "ticks", "1704067200000", "!!!"), true, 5,
        json_headers());
    expect_error_response(invalid_base64, 400, "invalid_query_param");

    const auto invalid_tf = node.request("POST", "/v1/ingest", make_ingest_body("binance", "BTCUSDT", "spot", "bad"),
                                         true, 5, json_headers());
    expect_error_response(invalid_tf, 400, "invalid_query_param");

    const auto negative_block_ts = node.request(
        "POST", "/v1/ingest", make_ingest_body("binance", "BTCUSDT", "spot", "ticks", "-1"), true, 5, json_headers());
    CHECK_EQ(negative_block_ts.status, 200);

    const auto object_body =
        node.request("POST", "/v1/ingest",
                     R"({"key":{"provider":"binance","symbol":"BTCUSDT","source":"spot","tf":"ticks","block_ts":1}})",
                     true, 5, json_headers());
    expect_error_response(object_body, 400, "invalid_query_param");

    const auto missing_param =
        node.request("GET", "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1");
    expect_error_response(missing_param, 400, "invalid_query_param");

    const auto invalid_range =
        node.request("GET", "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=2&to_ms=2");
    expect_error_response(invalid_range, 400, "invalid_query_param");

    const auto too_large_range =
        node.request("GET", "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=0&to_ms=2000");
    expect_error_response(too_large_range, 400, "range_too_large");

    const std::string long_provider(10 * 1024, 'p');
    const auto long_provider_resp =
        node.request("POST", "/v1/ingest", make_ingest_body(long_provider), true, 5, json_headers());
    CHECK_EQ(long_provider_resp.status, 200);

    const auto utf8_resp = node.request("POST", "/v1/ingest", make_ingest_body("биржа", "БТСЮСДТ", "спот", "ticks"),
                                        true, 5, json_headers());
    CHECK_EQ(utf8_resp.status, 200);
}

void test_size_limits() {
    {
        auto cfg = make_http_base_config();
        cfg.http.max_payload_bytes = 8;
        RunningHttpNode node(std::move(cfg), "token-http-size");
        const auto response = node.request("POST", "/v1/ingest", std::string(64, 'x'), true, 5, json_headers());
        expect_error_response(response, 413, "payload_too_large");
    }

    {
        auto cfg = make_http_base_config();
        cfg.http.history_max_bytes = 2;
        RunningHttpNode node(std::move(cfg), "token-http-history-size");

        const auto ingest = node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, json_headers());
        CHECK_EQ(ingest.status, 200);

        const auto history =
            node.request("GET", "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1704067200000"
                                "&to_ms=1704070800000&format=csv");
        expect_error_response(history, 413, "response_too_large");
    }
}

void test_anti_replay_failures() {
    auto cfg = make_http_base_config();
    cfg.security.anti_replay.enabled = true;
    cfg.security.anti_replay.max_skew_ms = 1000;
    cfg.security.anti_replay.nonce_ttl_ms = 60000;
    cfg.security.anti_replay.nonce_capacity = 128;
    cfg.security.anti_replay.require_for_scopes = dfh_node::to_scope_mask(dfh_node::Scope::Write);
    RunningHttpNode node(std::move(cfg), "token-http-ar");

    HttpHeaders missing_header = json_headers();
    missing_header.emplace("X-DFH-Timestamp", std::to_string(test_support::now_epoch_ms()));
    missing_header.emplace("X-DFH-Nonce", "0011223344556677");
    const auto missing = node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, missing_header);
    expect_error_response(missing, 400, "missing_anti_replay_headers");

    {
        HttpHeaders headers = json_headers();
        const std::string timestamp = std::to_string(test_support::now_epoch_ms() - 10000);
        const auto ar = make_http_anti_replay_headers(node.token(), "POST", "/v1/ingest", make_ingest_body(), timestamp,
                                                      "0011223344556677");
        headers.insert(ar.begin(), ar.end());
        const auto response = node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, headers);
        expect_error_response(response, 401, "anti_replay_failed");
    }

    {
        HttpHeaders headers = json_headers();
        const std::string timestamp = std::to_string(test_support::now_epoch_ms());
        const auto ar =
            make_http_anti_replay_headers(node.token(), "POST", "/v1/ingest", make_ingest_body(), timestamp, "short");
        headers.insert(ar.begin(), ar.end());
        const auto response = node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, headers);
        expect_error_response(response, 401, "anti_replay_failed");
    }

    {
        HttpHeaders headers = json_headers();
        const std::string timestamp = std::to_string(test_support::now_epoch_ms());
        const auto ar = make_http_anti_replay_headers(node.token(), "POST", "/v1/ingest", make_ingest_body(), timestamp,
                                                      "ZZ11223344556677");
        headers.insert(ar.begin(), ar.end());
        const auto response = node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, headers);
        expect_error_response(response, 401, "anti_replay_failed");
    }

    {
        HttpHeaders first_headers = json_headers();
        HttpHeaders second_headers = json_headers();
        const std::string timestamp = std::to_string(test_support::now_epoch_ms());
        const auto ar = make_http_anti_replay_headers(node.token(), "POST", "/v1/ingest", make_ingest_body(), timestamp,
                                                      "1111222233334444");
        first_headers.insert(ar.begin(), ar.end());
        second_headers.insert(ar.begin(), ar.end());

        const auto first = node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, first_headers);
        CHECK_EQ(first.status, 200);

        const auto second = node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, second_headers);
        expect_error_response(second, 401, "anti_replay_failed");
    }

    {
        HttpHeaders headers = json_headers();
        const std::string timestamp = std::to_string(test_support::now_epoch_ms());
        auto ar = make_http_anti_replay_headers(node.token(), "POST", "/v1/ingest", make_ingest_body(), timestamp,
                                                "5555666677778888");
        auto sig_it = ar.find("X-DFH-Signature");
        CHECK(sig_it != ar.end());
        sig_it->second[0] = sig_it->second[0] == '0' ? '1' : '0';
        headers.insert(ar.begin(), ar.end());

        const auto response = node.request("POST", "/v1/ingest", make_ingest_body(), true, 5, headers);
        expect_error_response(response, 401, "anti_replay_failed");
    }
}

} // namespace

int main() {
    test_auth_and_scope();
    test_content_type_and_dto_corpus();
    test_size_limits();
    test_anti_replay_failures();
    return 0;
}
