/// \file http_error_map.cpp
/// \brief Реализация таблицы маппинга ошибок transport-слоя в HTTP.
/// \details Поле `error` остаётся стабильным, `detail` предназначено только для диагностики.
///
#include "http_error_map.hpp"

#include <nlohmann/json.hpp>

namespace dfh_node::transport {
namespace {

std::pair<int, std::string> make_http_error(const int status, const std::string_view error_code,
                                            const std::string_view detail) {
    return {status, make_error_body(error_code, detail)};
}

} // namespace

std::pair<int, std::string> gate_error_to_http(const GateError &err) {
    switch (err.code) {
    case GateErrorCode::Unauthorized:
        return error_code_to_http("unauthorized", err.message);
    case GateErrorCode::Forbidden:
        return error_code_to_http("forbidden", err.message);
    case GateErrorCode::RateLimited:
        return error_code_to_http("rate_limited", err.message);
    case GateErrorCode::ConnectionLimited:
        return error_code_to_http("connection_limited", err.message);
    case GateErrorCode::UnsupportedOperation:
        return error_code_to_http("unsupported_operation", err.message);
    case GateErrorCode::AntiReplayFailed:
        return error_code_to_http("anti_replay_failed", err.message);
    case GateErrorCode::AntiReplayRequired:
        return error_code_to_http("anti_replay_required", err.message);
    case GateErrorCode::MissingAntiReplayHeaders:
        return error_code_to_http("missing_anti_replay_headers", err.message);
    case GateErrorCode::MissingAntiReplayFields:
        return error_code_to_http("missing_anti_replay_fields", err.message);
    default:
        return error_code_to_http("internal_error", err.message);
    }
}

std::pair<int, std::string> enqueue_rejected_to_http() {
    return error_code_to_http("queue_full", "Request queue is full");
}

std::pair<int, std::string> adapter_error_to_http(const std::string &error_code, const std::string &detail) {
    if (error_code == "not_found") {
        return error_code_to_http("not_found", detail);
    }

    if (error_code == "invalid_argument") {
        return error_code_to_http("invalid_argument", detail);
    }

    return error_code_to_http("internal_error", detail);
}

std::string make_error_body(const std::string_view error_code, const std::string_view detail) {
    nlohmann::json body;
    body["error"] = error_code;
    body["detail"] = detail;
    return body.dump();
}

std::pair<int, std::string> error_code_to_http(const std::string_view error_code, const std::string_view detail) {
    if (error_code == "unauthorized" || error_code == "anti_replay_failed") {
        return make_http_error(401, error_code, detail);
    }

    if (error_code == "forbidden" || error_code == "anti_replay_required") {
        return make_http_error(403, error_code, detail);
    }

    if (error_code == "not_found") {
        return make_http_error(404, error_code, detail);
    }

    if (error_code == "duplicate_name") {
        return make_http_error(409, error_code, detail);
    }

    if (error_code == "invalid_query_param" || error_code == "invalid_json" || error_code == "invalid_argument" ||
        error_code == "unsupported_operation" || error_code == "missing_anti_replay_headers" ||
        error_code == "missing_anti_replay_fields" || error_code == "range_too_large") {
        return make_http_error(400, error_code, detail);
    }

    if (error_code == "rate_limited" || error_code == "connection_limited") {
        return make_http_error(429, error_code, detail);
    }

    if (error_code == "payload_too_large" || error_code == "response_too_large") {
        return make_http_error(413, error_code, detail);
    }

    if (error_code == "timeout") {
        return make_http_error(504, error_code, detail);
    }

    if (error_code == "disk_low") {
        return make_http_error(507, error_code, detail);
    }

    if (error_code == "queue_full") {
        return make_http_error(503, error_code, detail);
    }

    return make_http_error(500, "internal_error", detail);
}

} // namespace dfh_node::transport
