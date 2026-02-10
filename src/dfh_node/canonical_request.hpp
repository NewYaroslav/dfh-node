/// \file canonical_request.hpp
/// \brief Канонизация HTTP/WS запросов и подпись HMAC-SHA256.
/// \details Формирует каноническая строка и выполняет вычисление/проверку подписи.
///
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace dfh_node {

/// \brief Поля HTTP-запроса для формирования канонической строки.
struct HttpCanonicalInput {
    std::string method;                                            ///< HTTP-метод (GET/POST/...).
    std::string path;                                              ///< Путь без query string.
    std::vector<std::pair<std::string, std::string>> query_params; ///< Query-параметры.
    std::string timestamp;                                         ///< Unix epoch ms (строка).
    std::string nonce;                                             ///< Hex lowercase, 16 символов.
    std::string body_hash;                                         ///< SHA-256 тела, hex 64 символа.
};

/// \brief Поля WS-управляющего сообщения для формирования канонической строки.
struct WsCanonicalInput {
    std::string endpoint;     ///< WS endpoint (например, /ws/msgpack).
    std::string op;           ///< Операция (ingest/history/subscribe).
    std::string msg_id;       ///< Идентификатор сообщения.
    std::string timestamp;    ///< Unix epoch ms (строка).
    std::string nonce;        ///< Hex lowercase, 16 символов.
    std::string payload_hash; ///< SHA-256 payload, hex 64 символа.
};

/// \brief Канонизирует Параметры строки запроса (decode -> sort -> re-encode).
/// \param params Пары key/value исходной query-строки.
/// \return Каноническая query-строка или пустая строка.
std::string canonicalize_query_string(const std::vector<std::pair<std::string, std::string>> &params);

/// \brief Формирует каноническая строка для HTTP.
/// \param input Данные HTTP-запроса.
/// \return Каноническая строка без завершающего '\n'.
std::string canonicalize_http(const HttpCanonicalInput &input);

/// \brief Формирует каноническая строка для WS управляющее сообщение.
/// \param input Данные WS-сообщения.
/// \return Каноническая строка без завершающего '\n'.
std::string canonicalize_ws(const WsCanonicalInput &input);

/// \brief Вычисляет HMAC-SHA256 подпись канонической строки.
/// \param canonical Каноническая строка.
/// \param signing_key Ключ подписи (сырые байты, ожидается 32 байта).
/// \param key_len Длина ключа подписи.
/// \return Подпись в hex в нижнем регистре.
std::string compute_signature(const std::string &canonical, const unsigned char *signing_key, std::size_t key_len);

/// \brief Проверяет HMAC-SHA256 подпись в константное время.
/// \param canonical Каноническая строка.
/// \param expected_signature Ожидаемая подпись в hex в нижнем регистре.
/// \param signing_key Ключ подписи (сырые байты, ожидается 32 байта).
/// \param key_len Длина ключа подписи.
/// \return true, если подпись совпадает, иначе false.
bool verify_signature(const std::string &canonical, const std::string &expected_signature,
                      const unsigned char *signing_key, std::size_t key_len);

/// \brief Вычисляет signing key в hex: SHA256(token).
/// \param token Открытый токен.
/// \return SHA-256(token) в hex в нижнем регистре.
std::string compute_signing_key_hex(const std::string &token);

} // namespace dfh_node
