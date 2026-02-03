/**
 * @file status.hpp
 * @brief Структуры статуса ноды для API и диагностики.
 * @details Снимок статуса формируется внешним компонентом.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace dfh_node {

/// @brief Снимок статуса ноды на конкретный момент времени.
/// @details Поля предназначены для вывода и мониторинга.
struct StatusSnapshot {
    std::string node_id; ///< Идентификатор ноды.
    std::string version; ///< Версия ПО.
    std::string build_info; ///< Информация о сборке.
    std::uint64_t uptime_ms = 0; ///< Время работы, мс.
    std::size_t peers_count = 0; ///< Количество peer-нод.
    std::string env; ///< Окружение (dev|staging|prod).
};

} // namespace dfh_node
