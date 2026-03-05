/// \file parse_result.hpp
/// \brief Общие типы результата парсинга для transport-слоя.
/// \details Предоставляет унифицированные `ParseError` и `ParseResult<T>`
/// для HTTP/WS парсеров DTO и протокольных сообщений.
///
#pragma once

#include <string>
#include <utility>
#include <variant>

namespace dfh_node::transport {

/// \brief Ошибка парсинга в формате `{error_code, detail}`.
using ParseError = std::pair<std::string, std::string>;

/// \brief Результат парсинга: DTO/сообщение или ошибка.
/// \tparam T Тип успешного результата.
template <typename T> using ParseResult = std::variant<T, ParseError>;

} // namespace dfh_node::transport
