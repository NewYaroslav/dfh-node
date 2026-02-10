/// \file canonical_request.hpp
/// \brief Канонизация HTTP/WS запросов и подпись HMAC-SHA256.
/// \details Формирует canonical string и выполняет вычисление/проверку подписи.
///
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace dfh_node {

/// \brief Поля HTTP запроса для формирования canonical string.
struct HttpCanonicalInput {
    std::string method;                                            ///< HTTP-метод (GET/POST/...).
    std::string path;                                              ///< Путь без query string.
    std::vector<std::pair<std::string, std::string>> query_params; ///< Query-параметры.
    std::string timestamp;                                         ///< Unix epoch ms (строка).
    std::string nonce;                                             ///< Hex lowercase, 16 символов.
    std::string body_hash;                                         ///< SHA-256 тела, hex 64 символа.
};

/// \brief Поля WS control-message для формирования canonical string.
struct WsCanonicalInput {
    std::string endpoint;     ///< WS endpoint (например, /ws/msgpack).
    std::string op;           ///< Операция (ingest/history/subscribe).
    std::string msg_id;       ///< Идентификатор сообщения.
    std::string timestamp;    ///< Unix epoch ms (строка).
    std::string nonce;        ///< Hex lowercase, 16 символов.
    std::string payload_hash; ///< SHA-256 payload, hex 64 символа.
};

/// \brief Канонизирует query-параметры (decode -> sort -> re-encode).
/// \param params Пары key/value исходной query-строки.
/// \return Каноническая query-строка или пустая строка.
std::string canonicalize_query_string(const std::vector<std::pair<std::string, std::string>> &params);

/// \brief Формирует canonical string для HTTP.
/// \param input Данные HTTP-запроса.
/// \return Каноническая строка без завершающего '\n'.
std::string canonicalize_http(const HttpCanonicalInput &input);

/// \brief Формирует canonical string для WS control-message.
/// \param input Данные WS-сообщения.
/// \return Каноническая строка без завершающего '\n'.
std::string canonicalize_ws(const WsCanonicalInput &input);

/// \brief Вычисляет HMAC-SHA256 подпись canonical string.
/// \param canonical Каноническая строка.
/// \param signing_key Ключ подписи (raw bytes, ожидается 32 байта).
/// \param key_len Длина ключа подписи.
/// \return Подпись в hex lowercase.
std::string compute_signature(const std::string &canonical, const unsigned char *signing_key, std::size_t key_len);

/// \brief Проверяет HMAC-SHA256 подпись в constant-time.
/// \param canonical Каноническая строка.
/// \param expected_signature Ожидаемая подпись в hex lowercase.
/// \param signing_key Ключ подписи (raw bytes, ожидается 32 байта).
/// \param key_len Длина ключа подписи.
/// \return true, если подпись совпадает, иначе false.
bool verify_signature(const std::string &canonical, const std::string &expected_signature,
                      const unsigned char *signing_key, std::size_t key_len);

/// \brief Вычисляет signing key в hex: SHA256(token).
/// \param token Plaintext токен.
/// \return SHA-256(token) в hex lowercase.
std::string compute_signing_key_hex(const std::string &token);

} // namespace dfh_node
