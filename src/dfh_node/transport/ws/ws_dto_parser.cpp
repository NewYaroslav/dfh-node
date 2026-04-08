/// \file ws_dto_parser.cpp
/// \brief Реализация парсинга WS payload в DTO адаптера.
/// \details Содержит проверки обязательных полей и преобразование типов для
/// `history`, `ingest` и `dfhbin`-веток.
///
#include "ws_dto_parser.hpp"

#include <openssl/evp.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dfh_node::transport {
namespace {

template <typename T> ParseResult<T> make_parse_error(std::string detail) {
    return ParseError{"invalid_argument", std::move(detail)};
}

template <typename T> ParseResult<T> make_parse_error(std::string code, std::string detail) {
    return ParseError{std::move(code), std::move(detail)};
}

bool parse_i64_strict(const std::string &value, std::int64_t &out) {
    if (value.empty()) {
        return false;
    }

    try {
        std::size_t pos = 0;
        const long long parsed = std::stoll(value, &pos, 10);
        if (pos != value.size()) {
            return false;
        }
        out = static_cast<std::int64_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_timeframe(const std::string &value, Timeframe &out) {
    if (value == "ticks") {
        out = Timeframe::Ticks;
        return true;
    }
    if (value == "m1") {
        out = Timeframe::M1;
        return true;
    }
    return false;
}

bool decode_base64(const std::string &encoded, std::vector<std::uint8_t> &decoded) {
    if (encoded.empty()) {
        decoded.clear();
        return true;
    }

    if ((encoded.size() % 4U) != 0U) {
        return false;
    }

    decoded.assign((encoded.size() / 4U) * 3U, 0);
    const int out_len = EVP_DecodeBlock(decoded.data(), reinterpret_cast<const unsigned char *>(encoded.data()),
                                        static_cast<int>(encoded.size()));
    if (out_len < 0) {
        return false;
    }

    std::size_t padding = 0;
    if (!encoded.empty() && encoded.back() == '=') {
        padding = 1;
        if (encoded.size() >= 2 && encoded[encoded.size() - 2] == '=') {
            padding = 2;
        }
    }

    decoded.resize(static_cast<std::size_t>(out_len) - padding);
    return true;
}

bool read_required_nonempty_string(const nlohmann::json &object, const char *field_name, std::string &out,
                                   std::string &detail) {
    if (!object.contains(field_name)) {
        detail = std::string("missing field: ") + field_name;
        return false;
    }

    const auto &field = object.at(field_name);
    if (!field.is_string()) {
        detail = std::string("field must be string: ") + field_name;
        return false;
    }

    out = field.get<std::string>();
    if (out.empty()) {
        detail = std::string("field must not be empty: ") + field_name;
        return false;
    }

    return true;
}

bool read_required_string(const nlohmann::json &object, const char *field_name, std::string &out, std::string &detail) {
    if (!object.contains(field_name)) {
        detail = std::string("missing field: ") + field_name;
        return false;
    }

    const auto &field = object.at(field_name);
    if (!field.is_string()) {
        detail = std::string("field must be string: ") + field_name;
        return false;
    }

    out = field.get<std::string>();
    return true;
}

bool read_required_i64(const nlohmann::json &object, const char *field_name, std::int64_t &out, std::string &detail) {
    if (!object.contains(field_name)) {
        detail = std::string("missing field: ") + field_name;
        return false;
    }

    const auto &field = object.at(field_name);
    if (field.is_number_integer()) {
        out = field.get<std::int64_t>();
        return true;
    }

    if (field.is_string()) {
        if (parse_i64_strict(field.get<std::string>(), out)) {
            return true;
        }
    }

    detail = std::string("field must be int64: ") + field_name;
    return false;
}

bool read_optional_u32(const nlohmann::json &object, const char *field_name, std::optional<std::uint32_t> &out,
                       std::string &detail) {
    const auto it = object.find(field_name);
    if (it == object.end()) {
        out = std::nullopt;
        return true;
    }

    if (!it->is_number_unsigned() && !it->is_number_integer()) {
        detail = std::string("field must be uint32: ") + field_name;
        return false;
    }

    if (it->is_number_integer()) {
        const std::int64_t signed_raw = it->get<std::int64_t>();
        if (signed_raw < 0) {
            detail = std::string("field must be uint32: ") + field_name;
            return false;
        }
    }

    const std::uint64_t raw = it->get<std::uint64_t>();
    if (raw > std::numeric_limits<std::uint32_t>::max()) {
        detail = std::string("field must be uint32: ") + field_name;
        return false;
    }

    out = static_cast<std::uint32_t>(raw);
    return true;
}

} // namespace

ParseResult<QueryHistoryRequest> parse_ws_history_payload(const nlohmann::json &payload, const config::WsConfig &cfg) {
    if (!payload.is_object()) {
        return make_parse_error<QueryHistoryRequest>("payload must be object");
    }

    QueryHistoryRequest request;
    std::string tf_text;
    std::string detail;
    if (!read_required_nonempty_string(payload, "provider", request.provider, detail) ||
        !read_required_nonempty_string(payload, "symbol", request.symbol, detail) ||
        !read_required_nonempty_string(payload, "source", request.source, detail) ||
        !read_required_nonempty_string(payload, "tf", tf_text, detail) ||
        !read_required_i64(payload, "from_ms", request.from_ms, detail) ||
        !read_required_i64(payload, "to_ms", request.to_ms, detail)) {
        return make_parse_error<QueryHistoryRequest>(detail);
    }

    if (!parse_timeframe(tf_text, request.tf)) {
        return make_parse_error<QueryHistoryRequest>("unknown tf");
    }

    if (!read_optional_u32(payload, "provider_id", request.provider_id, detail) ||
        !read_optional_u32(payload, "symbol_id", request.symbol_id, detail)) {
        return make_parse_error<QueryHistoryRequest>(detail);
    }

    if (request.from_ms >= request.to_ms) {
        return make_parse_error<QueryHistoryRequest>("from_ms must be < to_ms");
    }
    if (cfg.history_max_range_ms > 0 && (request.to_ms - request.from_ms) > cfg.history_max_range_ms) {
        return make_parse_error<QueryHistoryRequest>("range_too_large", "requested range exceeds history_max_range_ms");
    }

    return request;
}

ParseResult<IngestRequest> parse_ws_ingest_payload(const nlohmann::json &payload, const config::WsConfig &cfg) {
    if (!payload.is_object()) {
        return make_parse_error<IngestRequest>("payload must be object");
    }

    IngestRequest request;
    std::string tf_text;
    std::string payload_base64;
    std::string detail;
    if (!read_required_nonempty_string(payload, "provider", request.key.provider, detail) ||
        !read_required_nonempty_string(payload, "symbol", request.key.symbol, detail) ||
        !read_required_nonempty_string(payload, "source", request.key.source, detail) ||
        !read_required_nonempty_string(payload, "tf", tf_text, detail) ||
        !read_required_i64(payload, "block_ts", request.key.block_ts, detail) ||
        !read_required_string(payload, "payload_base64", payload_base64, detail)) {
        return make_parse_error<IngestRequest>(detail);
    }

    if (!parse_timeframe(tf_text, request.key.tf)) {
        return make_parse_error<IngestRequest>("unknown tf");
    }

    if (!decode_base64(payload_base64, request.payload)) {
        return make_parse_error<IngestRequest>("invalid payload_base64");
    }

    if (cfg.max_payload_bytes > 0 && request.payload.size() > static_cast<std::size_t>(cfg.max_payload_bytes)) {
        return make_parse_error<IngestRequest>("payload exceeds max_payload_bytes");
    }

    return request;
}

ParseResult<BlockKey> parse_ws_dfhbin_payload(const nlohmann::json &payload) {
    if (!payload.is_object()) {
        return make_parse_error<BlockKey>("payload must be object");
    }

    BlockKey key;
    std::string tf_text;
    std::string detail;
    if (!read_required_nonempty_string(payload, "provider", key.provider, detail) ||
        !read_required_nonempty_string(payload, "symbol", key.symbol, detail) ||
        !read_required_nonempty_string(payload, "source", key.source, detail) ||
        !read_required_nonempty_string(payload, "tf", tf_text, detail) ||
        !read_required_i64(payload, "block_ts", key.block_ts, detail)) {
        return make_parse_error<BlockKey>(detail);
    }

    if (!parse_timeframe(tf_text, key.tf)) {
        return make_parse_error<BlockKey>("unknown tf");
    }

    return key;
}

} // namespace dfh_node::transport
