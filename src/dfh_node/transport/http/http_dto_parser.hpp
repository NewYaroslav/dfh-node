/// \file http_dto_parser.hpp
/// \brief Парсинг HTTP DTO для `/v1/history` и `/v1/ingest`.
/// \details Преобразует query/body в DTO адаптера или возвращает стабильный `error_code` с деталями.
///
#pragma once

#include "adapter/dfh_adapter_dto.hpp"
#include "config/config.hpp"
#include "transport/parse_result.hpp"

#include <string>
#include <vector>

namespace dfh_node::transport {

/// \brief Распарсить query-параметры `GET /v1/history` в `QueryHistoryRequest`.
/// \param query_string Сырая query-строка без ведущего `?` или с ним.
/// \param cfg HTTP-конфигурация с лимитами.
/// \return `QueryHistoryRequest` либо `{error_code, detail}`.
ParseResult<QueryHistoryRequest> parse_history_query(const std::string &query_string, const config::HttpConfig &cfg);

/// \brief Распарсить JSON-тело `POST /v1/ingest` в список `IngestRequest`.
/// \param body Тело HTTP-запроса.
/// \param cfg HTTP-конфигурация с лимитами.
/// \return `std::vector<IngestRequest>` либо `{error_code, detail}`.
ParseResult<std::vector<IngestRequest>> parse_ingest_body(const std::string &body, const config::HttpConfig &cfg);

} // namespace dfh_node::transport
