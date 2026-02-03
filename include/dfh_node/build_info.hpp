/**
 * \file build_info.hpp
 * \brief Сведения о сборке, используемые для диагностики.
 * \details Источник строки сборки фиксируется на этапе компоновки.
 */
#pragma once

#include <string>

namespace dfh_node {

/// \brief Возвращает человекочитаемую строку сборки.
/// \return Строка с информацией о сборке (версия/тип сборки).
/// \throws Не бросает.
/// \note Потокобезопасно, функция не использует изменяемое состояние.
inline std::string build_info_string() { return "0.1.0-dev"; }

} // namespace dfh_node
