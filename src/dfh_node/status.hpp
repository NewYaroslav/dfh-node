/**
 * \file status.hpp
 * \brief Структуры статуса ноды для API и диагностики.
 * \details Снимок статуса формируется внешним компонентом.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace dfh_node {

/// \brief Метрики одной очереди (high-priority или low-priority).
struct QueueMetrics {
    std::size_t current_size = 0; ///< Текущее количество задач в очереди.
    std::size_t capacity = 0; ///< Максимальная вместимость очереди.
    std::uint64_t rejected_count = 0; ///< Количество отклонённых задач (очередь полна).
    std::uint64_t dropped_count = 0; ///< Количество сброшенных задач (ВСЕГДА 0 в Этапе 3, зарезервировано для WS).
    std::uint64_t total_enqueued = 0; ///< Всего задач поставлено в очередь.
    std::uint64_t total_processed = 0; ///< Всего задач обработано.
    double avg_wait_ms = 0.0; ///< Среднее время ожидания в очереди (мс).
};

/// \brief Снимок статуса ноды на конкретный момент времени.
/// \details Поля предназначены для вывода и мониторинга.
struct StatusSnapshot {
    std::string node_id; ///< Идентификатор ноды.
    std::string version; ///< Версия ПО.
    std::string build_info; ///< Информация о сборке.
    std::uint64_t uptime_ms = 0; ///< Время работы, мс.
    std::size_t peers_count = 0; ///< Количество peer-нод.
    std::string env; ///< Окружение (dev|staging|prod).
    QueueMetrics high_priority_queue; ///< Метрики high-priority очереди.
    QueueMetrics low_priority_queue; ///< Метрики low-priority очереди.
    int workers_count = 0; ///< Количество воркеров (из config.queues.workers).
};

} // namespace dfh_node
