/// \file transport_security_utils.hpp
/// \brief Переиспользуемые transport-утилиты для auth и anti-replay.
/// \details Централизует trim, извлечение Bearer-токена и разбор HTTP
/// anti-replay заголовков для HTTP/WS transport-слоя.

#pragma once

#include "security/anti_replay_fields.hpp"

#include <server_http.hpp>

#include <string>
#include <string_view>

namespace dfh_node::transport {

/// \brief Возвращает копию строки без пробелов по краям.
/// \param value Входное строковое представление.
/// \return Копия строки без лидирующих и хвостовых пробельных символов.
std::string trim_copy(std::string_view value);

/// \brief Извлекает Bearer-токен из HTTP/WS заголовков.
/// \param headers Набор заголовков transport-запроса.
/// \return Открытый токен без префикса `Bearer ` или пустую строку.
std::string extract_bearer_token(const SimpleWeb::CaseInsensitiveMultimap &headers);

/// \brief Разбирает HTTP anti-replay заголовки в DTO для `UnifiedGate`.
/// \param method HTTP-метод запроса.
/// \param path Нормализованный path запроса.
/// \param query_string Исходная query string, допускается ведущий `?`.
/// \param headers Набор HTTP-заголовков запроса.
/// \param body_hash SHA-256 тела в hex.
/// \param fields_out Выходной DTO при успешном разборе.
/// \param has_any_headers Возвращает, присутствовал ли хотя бы один anti-replay заголовок.
/// \return Указатель на `fields_out`, если заголовки полные и валидны для базового разбора; иначе `nullptr`.
const HttpAntiReplayFields *parse_http_anti_replay_fields(const std::string &method, const std::string &path,
                                                          const std::string &query_string,
                                                          const SimpleWeb::CaseInsensitiveMultimap &headers,
                                                          const std::string &body_hash,
                                                          HttpAntiReplayFields &fields_out, bool &has_any_headers);

} // namespace dfh_node::transport
