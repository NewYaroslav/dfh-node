/// \file ws_protocol.hpp
/// \brief Типы и функции протокола WebSocket transport-слоя.
/// \details Описывает парсинг control-message (json/msgpack) и сериализацию
/// ответов сервера в соответствующий формат.
///
#pragma once

#include "core/task.hpp"
#include "transport/parse_result.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dfh_node::transport {

/// \brief Поддерживаемая операция control-message.
enum class WsOp : std::uint8_t {
    Ingest = 0,    ///< Операция ingest.
    History = 1,   ///< Операция history.
    Subscribe = 2, ///< Операция subscribe.
};

/// \brief Формат transport-сообщения.
enum class WsFormat : std::uint8_t {
    Json = 0,    ///< Текстовый JSON.
    Msgpack = 1, ///< Бинарный MessagePack.
};

/// \brief Нормализованное WS control-message.
struct WsControlMessage {
    WsOp op = WsOp::History;
    std::string msg_id;       ///< Обязательный id корреляции request/response.
    nlohmann::json payload{}; ///< JSON-совместимый payload операции.
    WsFormat format = WsFormat::Json;
    std::string payload_hash;   ///< SHA-256 payload в hex (для anti-replay).
    std::string payload_sha256; ///< SHA-256 binary frame в hex (dfhbin).
    std::string timestamp;      ///< Unix epoch ms.
    std::string nonce;          ///< Nonce в hex.
    std::string signature;      ///< HMAC-SHA256 в hex.
};

/// \brief Нормализованное WS response-message.
struct WsResponseMessage {
    std::string msg_id;       ///< Id запроса, на который формируется ответ.
    bool ok = false;          ///< `true` для успешного ответа.
    std::string error_code;   ///< Стабильный код ошибки при `ok=false`.
    std::string detail;       ///< Нестабильная детализация при `ok=false`.
    nlohmann::json data = {}; ///< Полезные данные при `ok=true`.
};

/// \brief Распарсить JSON control-message.
/// \param text UTF-8 JSON-строка.
/// \return `WsControlMessage` либо `ParseError`.
ParseResult<WsControlMessage> parse_ws_json(const std::string &text);

/// \brief Распарсить MessagePack control-message.
/// \param bytes Сырые байты MessagePack.
/// \return `WsControlMessage` либо `ParseError`.
ParseResult<WsControlMessage> parse_ws_msgpack(const std::vector<std::uint8_t> &bytes);

/// \brief Сериализовать WS-ответ в JSON.
/// \param msg Ответ сервера.
/// \return JSON-строка.
std::string serialize_ws_json_response(const WsResponseMessage &msg);

/// \brief Сериализовать WS-ответ в MessagePack.
/// \param msg Ответ сервера.
/// \return Массив байт MessagePack.
std::vector<std::uint8_t> serialize_ws_msgpack_response(const WsResponseMessage &msg);

/// \brief Преобразовать строку операции в `WsOp`.
/// \param op_str Строковое имя операции (`ingest|history|subscribe`).
/// \return Значение `WsOp` или `std::nullopt` для неизвестной операции.
std::optional<WsOp> parse_ws_op(const std::string &op_str);

/// \brief Преобразовать `WsOp` в `TaskKind`.
/// \param op Операция control-message.
/// \return Соответствующий `TaskKind` (`Ingest`/`History`).
TaskKind ws_op_to_task_kind(WsOp op);

} // namespace dfh_node::transport
