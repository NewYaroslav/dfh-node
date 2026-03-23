/// \file sync_dto_parser.hpp
/// \brief Парсинг DTO для HTTP endpoints межнодовой синхронизации.
/// \details Преобразует JSON/query transport-формата в DTO адаптера и
/// возвращает стабильные коды ошибок для `/sync/*`.
///
#pragma once

#include "adapter/dfh_adapter_dto.hpp"
#include "transport/parse_result.hpp"

#include <string>

namespace dfh_node::transport {

/// \brief Разбирает JSON-тело `POST /sync/meta`.
/// \details Все поля фильтра опциональны; отсутствующие поля оставляют
/// значения DTO по умолчанию.
/// \param body Сырое тело HTTP-запроса.
/// \return `ListBlockMetaRequest` либо `{error_code, detail}`.
ParseResult<ListBlockMetaRequest> parse_sync_meta_body(const std::string &body);

/// \brief Разбирает query-параметры `GET /sync/block`.
/// \details Обязательные поля: `provider`, `symbol`, `source`, `tf`, `block_ts`.
/// \param query_string Сырая query-строка без ведущего `?` или с ним.
/// \return `GetBlockDfhbinRequest` либо `{error_code, detail}`.
ParseResult<GetBlockDfhbinRequest> parse_sync_block_query(const std::string &query_string);

} // namespace dfh_node::transport
