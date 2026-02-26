/// \file version.cpp
/// \brief Реализации доступа к версии и имени ноды.
/// \details Возвращает константы из public header без доп. логики.
///
#include "version.hpp"

namespace dfh_node {

std::string_view version() { return kVersion; }

std::string_view name() { return kNodeName; }

} // namespace dfh_node
