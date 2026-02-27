/// \file test_http_error_map.cpp
/// \brief Тесты таблицы маппинга transport-ошибок в HTTP.
/// \details Проверяет соответствие кодов `GateError`/adapter/parser/limit/timeout ожидаемым статусам.
///
#include "test_helpers.hpp"
#include "transport/http/http_error_map.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace {

void check_error_response(const std::pair<int, std::string> &response, int expected_status,
                          const std::string &expected_error, const std::string &expected_detail) {
    CHECK_EQ(response.first, expected_status);

    const nlohmann::json parsed = nlohmann::json::parse(response.second);
    CHECK(parsed.contains("error"));
    CHECK(parsed.contains("detail"));
    CHECK_EQ(parsed.at("error").get<std::string>(), expected_error);
    CHECK_EQ(parsed.at("detail").get<std::string>(), expected_detail);
}

void test_gate_error_mapping() {
    using namespace dfh_node;
    using namespace dfh_node::transport;

    check_error_response(gate_error_to_http({GateErrorCode::Unauthorized, "unauth"}), 401, "unauthorized", "unauth");
    check_error_response(gate_error_to_http({GateErrorCode::Forbidden, "forbid"}), 403, "forbidden", "forbid");
    check_error_response(gate_error_to_http({GateErrorCode::RateLimited, "rps"}), 429, "rate_limited", "rps");
    check_error_response(gate_error_to_http({GateErrorCode::ConnectionLimited, "ws"}), 429, "connection_limited", "ws");
    check_error_response(gate_error_to_http({GateErrorCode::UnsupportedOperation, "unsupported"}), 400,
                         "unsupported_operation", "unsupported");
    check_error_response(gate_error_to_http({GateErrorCode::AntiReplayFailed, "ar-failed"}), 401, "anti_replay_failed",
                         "ar-failed");
    check_error_response(gate_error_to_http({GateErrorCode::AntiReplayRequired, "ar-required"}), 403,
                         "anti_replay_required", "ar-required");
    check_error_response(gate_error_to_http({GateErrorCode::MissingAntiReplayHeaders, "ar-headers"}), 400,
                         "missing_anti_replay_headers", "ar-headers");
    check_error_response(gate_error_to_http({GateErrorCode::MissingAntiReplayFields, "ar-fields"}), 400,
                         "missing_anti_replay_fields", "ar-fields");
}

void test_enqueue_mapping() {
    using namespace dfh_node::transport;
    check_error_response(enqueue_rejected_to_http(), 503, "queue_full", "Request queue is full");
}

void test_adapter_mapping() {
    using namespace dfh_node::transport;

    check_error_response(adapter_error_to_http("not_found", "no-data"), 404, "not_found", "no-data");
    check_error_response(adapter_error_to_http("invalid_argument", "bad-arg"), 400, "invalid_argument", "bad-arg");
    check_error_response(adapter_error_to_http("internal", "oops"), 500, "internal_error", "oops");
    check_error_response(adapter_error_to_http("unknown_error", "unknown"), 500, "internal_error", "unknown");
}

void test_stable_error_code_mapping() {
    using namespace dfh_node::transport;

    check_error_response(error_code_to_http("invalid_query_param", "missing provider"), 400, "invalid_query_param",
                         "missing provider");
    check_error_response(error_code_to_http("invalid_json", "parse failure"), 400, "invalid_json", "parse failure");
    check_error_response(error_code_to_http("range_too_large", "narrow range"), 400, "range_too_large", "narrow range");
    check_error_response(error_code_to_http("response_too_large", "narrow range"), 413, "response_too_large",
                         "narrow range");
    check_error_response(error_code_to_http("payload_too_large", "payload > limit"), 413, "payload_too_large",
                         "payload > limit");
    check_error_response(error_code_to_http("timeout", "request timeout"), 504, "timeout", "request timeout");
}

} // namespace

int main() {
    test_gate_error_mapping();
    test_enqueue_mapping();
    test_adapter_mapping();
    test_stable_error_code_mapping();
    return 0;
}
