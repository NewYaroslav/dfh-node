/// \file sync_dto_parser.cpp
/// \brief Реализация парсинга DTO для HTTP sync-endpoints.
/// \details Содержит разбор JSON-фильтра `/sync/meta` и query-параметров
/// `/sync/block` без привязки к runtime-логике роутера.
///
#include "sync_dto_parser.hpp"

#include <nlohmann/json.hpp>
#include <server_http.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace dfh_node::transport {
namespace {

template <typename T> ParseResult<T> make_parse_error(std::string error_code, std::string detail) {
    return ParseError{std::move(error_code), std::move(detail)};
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

std::string normalize_query_string(const std::string &query_string) {
    if (!query_string.empty() && query_string.front() == '?') {
        return query_string.substr(1);
    }
    return query_string;
}

} // namespace

ParseResult<ListBlockMetaRequest> parse_sync_meta_body(const std::string &body) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(body);
    } catch (const nlohmann::json::parse_error &error) {
        return make_parse_error<ListBlockMetaRequest>("invalid_json", error.what());
    }

    if (!root.is_object()) {
        return make_parse_error<ListBlockMetaRequest>("invalid_query_param", "sync meta body must be object");
    }

    ListBlockMetaRequest request;

    auto parse_optional_string = [&](const char *field_name, std::string &target) -> bool {
        if (!root.contains(field_name) || root.at(field_name).is_null()) {
            return true;
        }
        if (!root.at(field_name).is_string()) {
            return false;
        }
        target = root.at(field_name).get<std::string>();
        return true;
    };

    if (!parse_optional_string("provider", request.provider) || !parse_optional_string("symbol", request.symbol) ||
        !parse_optional_string("source", request.source)) {
        return make_parse_error<ListBlockMetaRequest>("invalid_query_param", "provider/symbol/source must be string");
    }

    if (root.contains("tf") && !root.at("tf").is_null()) {
        if (!root.at("tf").is_string()) {
            return make_parse_error<ListBlockMetaRequest>("invalid_query_param", "field must be string: tf");
        }
        if (!parse_timeframe(root.at("tf").get<std::string>(), request.tf)) {
            return make_parse_error<ListBlockMetaRequest>("invalid_query_param", "unknown tf");
        }
    }

    auto parse_optional_i64 = [&](const char *field_name, std::optional<std::int64_t> &target) -> bool {
        if (!root.contains(field_name) || root.at(field_name).is_null()) {
            return true;
        }
        if (root.at(field_name).is_number_integer()) {
            target = root.at(field_name).get<std::int64_t>();
            return true;
        }
        if (root.at(field_name).is_string()) {
            std::int64_t parsed = 0;
            if (parse_i64_strict(root.at(field_name).get<std::string>(), parsed)) {
                target = parsed;
                return true;
            }
        }
        return false;
    };

    if (!parse_optional_i64("from_block_ts", request.from_block_ts)) {
        return make_parse_error<ListBlockMetaRequest>("invalid_query_param", "field must be int64: from_block_ts");
    }

    if (!parse_optional_i64("to_block_ts", request.to_block_ts)) {
        return make_parse_error<ListBlockMetaRequest>("invalid_query_param", "field must be int64: to_block_ts");
    }

    return request;
}

ParseResult<GetBlockDfhbinRequest> parse_sync_block_query(const std::string &query_string) {
    const auto params = SimpleWeb::QueryString::parse(normalize_query_string(query_string));

    static const char *kRequiredFields[] = {"provider", "symbol", "source", "tf", "block_ts"};
    for (const char *field_name : kRequiredFields) {
        if (params.find(field_name) == params.end()) {
            return make_parse_error<GetBlockDfhbinRequest>("invalid_query_param",
                                                           std::string("missing field: ") + field_name);
        }
    }

    GetBlockDfhbinRequest request;
    request.key.provider = params.find("provider")->second;
    request.key.symbol = params.find("symbol")->second;
    request.key.source = params.find("source")->second;

    if (!parse_timeframe(params.find("tf")->second, request.key.tf)) {
        return make_parse_error<GetBlockDfhbinRequest>("invalid_query_param", "unknown tf");
    }

    if (!parse_i64_strict(params.find("block_ts")->second, request.key.block_ts)) {
        return make_parse_error<GetBlockDfhbinRequest>("invalid_query_param", "block_ts must be int64");
    }

    return request;
}

} // namespace dfh_node::transport
