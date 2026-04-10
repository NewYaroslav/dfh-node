/// \file time_utils.cpp
/// \brief Реализация общих утилит времени для Unix epoch.
/// \details Централизует получение `system_clock` в миллисекундах без
/// дублирования по доменам.

#include "time_utils.hpp"

#include <chrono>

namespace dfh_node {

std::int64_t now_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::uint64_t steady_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace dfh_node
