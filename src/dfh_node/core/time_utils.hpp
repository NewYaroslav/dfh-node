/// \file time_utils.hpp
/// \brief Общие утилиты времени для Unix epoch.
/// \details Выносит повторяющееся вычисление `system_clock` в миллисекундах
/// в единый модуль.

#pragma once

#include <cstdint>

namespace dfh_node {

/// \brief Возвращает текущее Unix-время в миллисекундах.
/// \return Текущее количество миллисекунд от Unix epoch.
std::int64_t now_epoch_ms();

/// \brief Возвращает монотонное время в миллисекундах (`steady_clock`).
/// \return Количество миллисекунд от произвольной точки отсчёта.
std::uint64_t steady_ms();

} // namespace dfh_node
