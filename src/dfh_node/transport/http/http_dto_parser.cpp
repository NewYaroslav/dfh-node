/// \file http_dto_parser.cpp
/// \brief Реализация парсинга HTTP DTO для transport-слоя.
/// \details Содержит разбор query-параметров истории и JSON-массива ingest-блоков.
///
#include "http_dto_parser.hpp"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <string_view>
#include <type_traits>

namespace dfh_node::transport {
namespace {

constexpr std::int64_t kDefaultHistoryMaxRangeMs = 86400000;

template <typename T, typename = void> struct history_range_accessor {
    static std::int64_t get(const T &) { return kDefaultHistoryMaxRangeMs; }
};

template <typename T> struct history_range_accessor<T, std::void_t<decltype(std::declval<T>().history_max_range_ms)>> {
    static std::int64_t get(const T &cfg) { return cfg.history_max_range_ms; }
};

template <typename T> ParseResult<T> make_parse_error(std::string error_code, std::string detail) {
    return ParseError{std::move(error_code), std::move(detail)};
}

std::int64_t resolve_history_range_limit(const config::HttpConfig &cfg) {
    return history_range_accessor<config::HttpConfig>::get(cfg);
}

int hex_value(const char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return (ch - 'a') + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return (ch - 'A') + 10;
    }
    return -1;
}

bool url_decode(const std::string &input, std::string &output) {
    output.clear();
    output.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        if (ch == '+') {
            output.push_back(' ');
            continue;
        }

        if (ch == '%') {
            if (i + 2 >= input.size()) {
                return false;
            }

            const int hi = hex_value(input[i + 1]);
            const int lo = hex_value(input[i + 2]);
            if (hi < 0 || lo < 0) {
                return false;
            }

            output.push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
            continue;
        }

        output.push_back(ch);
    }

    return true;
}

bool parse_query_string(const std::string &query_string, std::map<std::string, std::string> &params,
                        std::string &error_detail) {
    params.clear();

    const std::string_view input = (!query_string.empty() && query_string.front() == '?')
                                       ? std::string_view(query_string).substr(1)
                                       : std::string_view(query_string);

    std::size_t start = 0;
    while (start <= input.size()) {
        const std::size_t amp = input.find('&', start);
        const std::string_view token =
            (amp == std::string_view::npos) ? input.substr(start) : input.substr(start, amp - start);

        if (!token.empty()) {
            const std::size_t eq = token.find('=');
            const std::string_view raw_key = token.substr(0, eq);
            const std::string_view raw_value =
                (eq == std::string_view::npos) ? std::string_view() : token.substr(eq + 1);

            std::string key;
            std::string value;
            if (!url_decode(std::string(raw_key), key) || !url_decode(std::string(raw_value), value)) {
                error_detail = "invalid percent-encoding in query";
                return false;
            }

            params[std::move(key)] = std::move(value);
        }

        if (amp == std::string_view::npos) {
            break;
        }
        start = amp + 1;
    }

    return true;
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

bool parse_u32_strict(const std::string &value, std::uint32_t &out) {
    if (value.empty()) {
        return false;
    }

    try {
        std::size_t pos = 0;
        const unsigned long parsed = std::stoul(value, &pos, 10);
        if (pos != value.size() || parsed > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        out = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_timeframe(const std::string &value, Timeframe &tf_out) {
    if (value == "ticks") {
        tf_out = Timeframe::Ticks;
        return true;
    }
    if (value == "m1") {
        tf_out = Timeframe::M1;
        return true;
    }
    return false;
}

bool get_required_string(const nlohmann::json &object, const char *field_name, const char *field_path, std::string &out,
                         std::string &error_detail) {
    if (!object.contains(field_name)) {
        error_detail = std::string("missing field: ") + field_path;
        return false;
    }

    const auto &field = object.at(field_name);
    if (!field.is_string()) {
        error_detail = std::string("field must be string: ") + field_path;
        return false;
    }

    out = field.get<std::string>();
    return true;
}

bool get_required_i64(const nlohmann::json &object, const char *field_name, const char *field_path, std::int64_t &out,
                      std::string &error_detail) {
    if (!object.contains(field_name)) {
        error_detail = std::string("missing field: ") + field_path;
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

    error_detail = std::string("field must be int64: ") + field_path;
    return false;
}

bool decode_base64(const std::string &encoded, std::vector<std::uint8_t> &decoded) {
    if (encoded.empty()) {
        decoded.clear();
        return true;
    }

    if ((encoded.size() % 4) != 0U) {
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

template <typename T>
bool parse_optional_u32(const std::map<std::string, std::string> &params, const char *key, std::optional<T> &out,
                        std::string &error_detail) {
    const auto it = params.find(key);
    if (it == params.end()) {
        return true;
    }

    std::uint32_t parsed = 0;
    if (!parse_u32_strict(it->second, parsed)) {
        error_detail = std::string("invalid uint32 field: ") + key;
        return false;
    }

    out = static_cast<T>(parsed);
    return true;
}

} // namespace

ParseResult<QueryHistoryRequest> parse_history_query(const std::string &query_string, const config::HttpConfig &cfg) {
    std::map<std::string, std::string> params;
    std::string parse_error_detail;
    if (!parse_query_string(query_string, params, parse_error_detail)) {
        return make_parse_error<QueryHistoryRequest>("invalid_query_param", parse_error_detail);
    }

    static const char *kRequiredFields[] = {"provider", "symbol", "source", "tf", "from_ms", "to_ms"};
    for (const char *field_name : kRequiredFields) {
        if (params.find(field_name) == params.end()) {
            return make_parse_error<QueryHistoryRequest>("invalid_query_param",
                                                         std::string("missing field: ") + field_name);
        }
    }

    QueryHistoryRequest request;
    request.provider = params["provider"];
    request.symbol = params["symbol"];
    request.source = params["source"];

    if (!parse_timeframe(params["tf"], request.tf)) {
        return make_parse_error<QueryHistoryRequest>("invalid_query_param", "unknown tf");
    }

    if (!parse_i64_strict(params["from_ms"], request.from_ms)) {
        return make_parse_error<QueryHistoryRequest>("invalid_query_param", "from_ms must be int64");
    }

    if (!parse_i64_strict(params["to_ms"], request.to_ms)) {
        return make_parse_error<QueryHistoryRequest>("invalid_query_param", "to_ms must be int64");
    }

    if (request.from_ms >= request.to_ms) {
        return make_parse_error<QueryHistoryRequest>("invalid_query_param", "from_ms must be < to_ms");
    }

    const std::int64_t max_range_ms = resolve_history_range_limit(cfg);
    const std::int64_t range_ms = request.to_ms - request.from_ms;
    if (max_range_ms > 0 && range_ms > max_range_ms) {
        return make_parse_error<QueryHistoryRequest>("range_too_large", "requested range exceeds history_max_range_ms");
    }

    const auto format_it = params.find("format");
    const std::string format = (format_it == params.end()) ? "csv" : format_it->second;
    if (format != "csv" && format != "dfhbin") {
        return make_parse_error<QueryHistoryRequest>("invalid_query_param", "unknown format");
    }

    if (!parse_optional_u32(params, "provider_id", request.provider_id, parse_error_detail)) {
        return make_parse_error<QueryHistoryRequest>("invalid_query_param", parse_error_detail);
    }

    if (!parse_optional_u32(params, "symbol_id", request.symbol_id, parse_error_detail)) {
        return make_parse_error<QueryHistoryRequest>("invalid_query_param", parse_error_detail);
    }

    return request;
}

ParseResult<std::vector<IngestRequest>> parse_ingest_body(const std::string &body, const config::HttpConfig &cfg) {
    if (cfg.max_payload_bytes > 0 && body.size() > static_cast<std::size_t>(cfg.max_payload_bytes)) {
        return make_parse_error<std::vector<IngestRequest>>("payload_too_large", "body exceeds max_payload_bytes");
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(body);
    } catch (const nlohmann::json::parse_error &e) {
        return make_parse_error<std::vector<IngestRequest>>("invalid_json", e.what());
    }

    if (!root.is_array()) {
        return make_parse_error<std::vector<IngestRequest>>("invalid_query_param", "ingest body must be array");
    }

    if (root.empty()) {
        return make_parse_error<std::vector<IngestRequest>>("invalid_query_param", "empty array");
    }

    std::vector<IngestRequest> requests;
    requests.reserve(root.size());

    for (std::size_t index = 0; index < root.size(); ++index) {
        const auto &item = root[index];
        if (!item.is_object()) {
            return make_parse_error<std::vector<IngestRequest>>(
                "invalid_query_param", std::string("ingest item must be object at index: ") + std::to_string(index));
        }

        if (!item.contains("key") || !item.at("key").is_object()) {
            return make_parse_error<std::vector<IngestRequest>>("invalid_query_param", "missing field: key");
        }

        const auto &key = item.at("key");
        std::string provider;
        std::string symbol;
        std::string source;
        std::string tf_string;
        std::string payload_base64;
        std::string error_detail;
        std::int64_t block_ts = 0;

        if (!get_required_string(key, "provider", "key.provider", provider, error_detail) ||
            !get_required_string(key, "symbol", "key.symbol", symbol, error_detail) ||
            !get_required_string(key, "source", "key.source", source, error_detail) ||
            !get_required_string(key, "tf", "key.tf", tf_string, error_detail) ||
            !get_required_i64(key, "block_ts", "key.block_ts", block_ts, error_detail) ||
            !get_required_string(item, "payload_base64", "payload_base64", payload_base64, error_detail)) {
            return make_parse_error<std::vector<IngestRequest>>("invalid_query_param", error_detail);
        }

        Timeframe tf = Timeframe::Ticks;
        if (!parse_timeframe(tf_string, tf)) {
            return make_parse_error<std::vector<IngestRequest>>("invalid_query_param", "unknown tf");
        }

        std::vector<std::uint8_t> payload;
        if (!decode_base64(payload_base64, payload)) {
            return make_parse_error<std::vector<IngestRequest>>("invalid_query_param", "invalid payload_base64");
        }

        IngestRequest request;
        request.key.provider = std::move(provider);
        request.key.symbol = std::move(symbol);
        request.key.source = std::move(source);
        request.key.tf = tf;
        request.key.block_ts = block_ts;
        request.payload = std::move(payload);
        requests.push_back(std::move(request));
    }

    return requests;
}

} // namespace dfh_node::transport
