/// \file admin_dto_parser.cpp
/// \brief Реализация парсинга DTO для HTTP Admin API.
/// \details Проверяет типы полей, непустое имя ключа и корректность списка
/// scope.
///
#include "admin_dto_parser.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace dfh_node::transport {
namespace {

template <typename T> DtoParseResult<T> make_parse_error(std::string detail) {
    return DtoParseError{std::move(detail)};
}

template <typename T>
bool parse_optional_int64_field(const nlohmann::json &object, const char *field_name, std::optional<T> &out,
                                std::string &error_detail) {
    if (!object.contains(field_name)) {
        return true;
    }

    const auto &field = object.at(field_name);
    if (!field.is_number_integer()) {
        error_detail = std::string("field must be int64: ") + field_name;
        return false;
    }

    out = static_cast<T>(field.get<std::int64_t>());
    return true;
}

bool parse_scopes_field(const nlohmann::json &object, ScopeMask &scope_mask, std::string &error_detail) {
    if (!object.contains("scopes")) {
        scope_mask = 0;
        return true;
    }

    const auto &field = object.at("scopes");
    if (!field.is_array()) {
        error_detail = "field must be array: scopes";
        return false;
    }

    scope_mask = 0;
    for (std::size_t index = 0; index < field.size(); ++index) {
        const auto &item = field.at(index);
        if (!item.is_string()) {
            error_detail = "scope must be string";
            return false;
        }

        const std::string scope_name = item.get<std::string>();
        const std::optional<Scope> scope = parse_scope(scope_name);
        if (!scope.has_value()) {
            error_detail = "unknown scope: " + scope_name;
            return false;
        }

        scope_mask = static_cast<ScopeMask>(scope_mask | *scope);
    }

    return true;
}

bool parse_name_field(const nlohmann::json &object, const bool required, std::optional<std::string> &name_out,
                      std::string &error_detail) {
    if (!object.contains("name")) {
        if (required) {
            error_detail = "missing field: name";
            return false;
        }
        return true;
    }

    const auto &field = object.at("name");
    if (!field.is_string()) {
        error_detail = "field must be string: name";
        return false;
    }

    const std::string value = field.get<std::string>();
    if (value.empty()) {
        error_detail = "field must be non-empty string: name";
        return false;
    }

    name_out = value;
    return true;
}

bool parse_expires_at_field(const nlohmann::json &object, std::optional<std::optional<std::int64_t>> &expires_at_out,
                            std::string &error_detail) {
    if (!object.contains("expires_at_ms")) {
        return true;
    }

    const auto &field = object.at("expires_at_ms");
    if (field.is_null()) {
        expires_at_out = std::optional<std::int64_t>{};
        return true;
    }

    if (!field.is_number_integer()) {
        error_detail = "field must be int64: expires_at_ms";
        return false;
    }

    expires_at_out = field.get<std::int64_t>();
    return true;
}

} // namespace

DtoParseResult<CreateKeyRequest> parse_create_key_request(const nlohmann::json &j) {
    if (!j.is_object()) {
        return make_parse_error<CreateKeyRequest>("request body must be object");
    }

    CreateKeyRequest request;
    std::optional<std::string> name;
    std::string error_detail;
    if (!parse_name_field(j, true, name, error_detail)) {
        return make_parse_error<CreateKeyRequest>(error_detail);
    }
    request.name = std::move(*name);

    if (!parse_scopes_field(j, request.scope_mask, error_detail)) {
        return make_parse_error<CreateKeyRequest>(error_detail);
    }

    std::optional<std::int64_t> rps_limit;
    if (!parse_optional_int64_field(j, "rps_limit", rps_limit, error_detail)) {
        return make_parse_error<CreateKeyRequest>(error_detail);
    }
    if (rps_limit.has_value()) {
        request.rps_limit = *rps_limit;
    }

    std::optional<std::int64_t> ws_max_connections;
    if (!parse_optional_int64_field(j, "ws_max_connections", ws_max_connections, error_detail)) {
        return make_parse_error<CreateKeyRequest>(error_detail);
    }
    if (ws_max_connections.has_value()) {
        request.ws_max_connections = *ws_max_connections;
    }

    std::optional<std::optional<std::int64_t>> expires_at_ms;
    if (!parse_expires_at_field(j, expires_at_ms, error_detail)) {
        return make_parse_error<CreateKeyRequest>(error_detail);
    }
    if (expires_at_ms.has_value()) {
        request.expires_at_ms = *expires_at_ms;
    }

    return request;
}

DtoParseResult<UpdateKeyRequest> parse_update_key_request(const nlohmann::json &j) {
    if (!j.is_object()) {
        return make_parse_error<UpdateKeyRequest>("request body must be object");
    }

    UpdateKeyRequest request;
    std::string error_detail;

    if (!parse_name_field(j, false, request.name, error_detail)) {
        return make_parse_error<UpdateKeyRequest>(error_detail);
    }

    if (j.contains("scopes")) {
        ScopeMask scope_mask = 0;
        if (!parse_scopes_field(j, scope_mask, error_detail)) {
            return make_parse_error<UpdateKeyRequest>(error_detail);
        }
        request.scope_mask = scope_mask;
    }

    if (!parse_optional_int64_field(j, "rps_limit", request.rps_limit, error_detail)) {
        return make_parse_error<UpdateKeyRequest>(error_detail);
    }

    if (!parse_optional_int64_field(j, "ws_max_connections", request.ws_max_connections, error_detail)) {
        return make_parse_error<UpdateKeyRequest>(error_detail);
    }

    if (!parse_expires_at_field(j, request.expires_at_ms, error_detail)) {
        return make_parse_error<UpdateKeyRequest>(error_detail);
    }

    return request;
}

} // namespace dfh_node::transport
