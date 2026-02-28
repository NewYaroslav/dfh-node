/// \file ws_dto_parser.hpp
/// \brief Парсинг payload WS control-message в DTO адаптера.
/// \details Проверяет обязательные поля, типы и базовые инварианты для
/// операций `history`, `ingest` и `dfhbin`.
///
#pragma once

#include "adapter/dfh_adapter_dto.hpp"
#include "config/config.hpp"
#include "transport/parse_result.hpp"

#include <nlohmann/json.hpp>

namespace dfh_node::transport {

/// \brief Распарсить payload `op=history` в `QueryHistoryRequest`.
/// \param payload JSON payload операции.
/// \param cfg Конфигурация WS с лимитами.
/// \return DTO запроса истории либо `ParseError` с кодом `invalid_argument`.
ParseResult<QueryHistoryRequest> parse_ws_history_payload(const nlohmann::json &payload, const config::WsConfig &cfg);

/// \brief Распарсить payload `op=ingest` (structured) в `IngestRequest`.
/// \param payload JSON payload операции.
/// \param cfg Конфигурация WS с лимитами.
/// \return DTO ingest либо `ParseError` с кодом `invalid_argument`.
ParseResult<IngestRequest> parse_ws_ingest_payload(const nlohmann::json &payload, const config::WsConfig &cfg);

/// \brief Распарсить payload `op=ingest` (dfhbin-control) в `BlockKey`.
/// \details Поле `payload_sha256` берётся из control-message; binary frame
/// приходит следующим WS-сообщением.
/// \param payload JSON payload операции.
/// \return `BlockKey` либо `ParseError` с кодом `invalid_argument`.
ParseResult<BlockKey> parse_ws_dfhbin_payload(const nlohmann::json &payload);

} // namespace dfh_node::transport
