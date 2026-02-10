/// \file version.hpp
/// \brief Публичные идентификаторы версии и имени ноды.
/// \details Используется для логирования, диагностики и smoke-тестов.
///
#pragma once

#include <string_view>

namespace dfh_node {

/// \brief Строка версии библиотеки/приложения.
/// \details Значение неизменяемо и используется только для диагностики.
constexpr std::string_view kVersion = "0.1.0";
/// \brief Каноническое имя ноды.
/// \details Используется в логах и внешних идентификаторах.
constexpr std::string_view kNodeName = "dfh-node";

/// \brief Возвращает строку версии ноды.
/// \return Строка версии.
/// \throws Не бросает.
/// \note Потокобезопасно, состояние не изменяется.
std::string_view version();
/// \brief Возвращает каноническое имя ноды.
/// \return Имя ноды.
/// \throws Не бросает.
/// \note Потокобезопасно, состояние не изменяется.
std::string_view name();

} // namespace dfh_node


