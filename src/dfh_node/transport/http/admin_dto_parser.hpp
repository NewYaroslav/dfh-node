/// \file admin_dto_parser.hpp
/// \brief Парсинг DTO для HTTP Admin API.
/// \details Преобразует JSON-тело Admin API в transport-DTO и возвращает
/// детальную ошибку формата без привязки к HTTP-статусам.
///
#pragma once

#include "auth/scope.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace dfh_node::transport {

/// \brief DTO создания динамического API-ключа.
struct CreateKeyRequest {
    std::string name;                          ///< Уникальное имя ключа.
    ScopeMask scope_mask = 0;                  ///< Битовая маска scope.
    std::int64_t rps_limit = 0;                ///< Индивидуальный RPS-лимит; `0` означает наследование.
    std::int64_t ws_max_connections = 0;       ///< Индивидуальный WS-лимит; `0` означает наследование.
    std::optional<std::int64_t> expires_at_ms; ///< Срок действия; `std::nullopt` означает бессрочно.
};

/// \brief DTO частичного обновления динамического API-ключа.
struct UpdateKeyRequest {
    std::optional<std::string> name;                          ///< Новое уникальное имя ключа.
    std::optional<ScopeMask> scope_mask;                      ///< Новая битовая маска scope.
    std::optional<std::int64_t> rps_limit;                    ///< Новый RPS-лимит.
    std::optional<std::int64_t> ws_max_connections;           ///< Новый WS-лимит.
    std::optional<std::optional<std::int64_t>> expires_at_ms; ///< Явное обновление срока действия.
};

/// \brief Ошибка парсинга DTO Admin API.
struct DtoParseError {
    std::string detail; ///< Диагностическое описание ошибки формата.
};

/// \brief Результат парсинга DTO Admin API.
/// \tparam T Тип успешного DTO.
template <typename T> using DtoParseResult = std::variant<T, DtoParseError>;

/// \brief Парсит JSON-тело создания API-ключа.
/// \param j Корневой JSON-объект запроса.
/// \return DTO создания или описание ошибки формата.
DtoParseResult<CreateKeyRequest> parse_create_key_request(const nlohmann::json &j);

/// \brief Парсит JSON-тело частичного обновления API-ключа.
/// \param j Корневой JSON-объект запроса.
/// \return DTO обновления или описание ошибки формата.
DtoParseResult<UpdateKeyRequest> parse_update_key_request(const nlohmann::json &j);

} // namespace dfh_node::transport
