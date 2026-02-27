/// \file http_error_map.hpp
/// \brief Единая таблица маппинга внутренних ошибок в HTTP-ответы.
/// \details Формирует стабильный код в поле `error` и опциональную детализацию в `detail`.
///
#pragma once

#include "auth/auth_service.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace dfh_node::transport {

/// \brief Преобразует `GateError` в HTTP-статус и JSON-тело.
/// \param err Ошибка авторизации/gate-проверки.
/// \return Пара `{http_status, json_body}`.
std::pair<int, std::string> gate_error_to_http(const GateError &err);

/// \brief Возвращает HTTP-ответ для `EnqueueStatus::Rejected`.
/// \return Пара `{503, json_body}` с кодом `queue_full`.
std::pair<int, std::string> enqueue_rejected_to_http();

/// \brief Преобразует ошибку адаптера в HTTP-статус и JSON-тело.
/// \param error_code Внутренний код адаптера (`not_found`, `invalid_argument`, `internal`, ...).
/// \param detail Нестабильная диагностическая строка для логов.
/// \return Пара `{http_status, json_body}`.
std::pair<int, std::string> adapter_error_to_http(const std::string &error_code, const std::string &detail = "");

/// \brief Собирает JSON-тело ответа ошибки.
/// \param error_code Стабильный код ошибки для клиентской логики.
/// \param detail Нестабильная диагностическая строка.
/// \return Строка JSON с полями `error` и `detail`.
std::string make_error_body(std::string_view error_code, std::string_view detail = "");

/// \brief Преобразует стабильный `error`-код в HTTP-статус и JSON-тело.
/// \param error_code Стабильный код ошибки transport-слоя.
/// \param detail Нестабильная диагностическая строка.
/// \return Пара `{http_status, json_body}`.
std::pair<int, std::string> error_code_to_http(std::string_view error_code, std::string_view detail = "");

} // namespace dfh_node::transport
