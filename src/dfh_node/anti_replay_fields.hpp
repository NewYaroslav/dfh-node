/// \file anti_replay_fields.hpp
/// \brief Transport-agnostic структуры для anti-replay полей.
/// \details UnifiedGate не зависит от nlohmann::json; парсинг JSON/MessagePack выполняется в transport-слое.
///
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace dfh_node {

/// \brief HTTP anti-replay поля.
struct HttpAntiReplayFields {
    std::string method; ///< HTTP-метод.
    std::string path; ///< Путь запроса.
    std::vector<std::pair<std::string, std::string>> query_params; ///< Query-параметры.
    std::string timestamp; ///< Unix epoch ms (13 цифр).
    std::string nonce; ///< Hex lowercase (16 символов).
    std::string signature; ///< HMAC-SHA256 в hex (64 символа).
    std::string body_hash; ///< SHA-256 тела в hex (64 символа).
};

/// \brief WS anti-replay поля.
struct WsAntiReplayFields {
    std::string endpoint; ///< WS endpoint.
    std::string op; ///< Операция.
    std::string msg_id; ///< Идентификатор сообщения.
    std::string timestamp; ///< Unix epoch ms (13 цифр).
    std::string nonce; ///< Hex lowercase (16 символов).
    std::string signature; ///< HMAC-SHA256 в hex (64 символа).
    std::string payload_hash; ///< SHA-256 payload в hex (64 символа).
};

} // namespace dfh_node
