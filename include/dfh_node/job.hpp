/**
 * \file job.hpp
 * \brief Модель задач и результаты постановки в очередь.
 * \details Содержит JobKind, EnqueueResult и правила работы с payload.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace dfh_node {

/// \brief Тип задачи для приоритетного планировщика.
enum class JobKind : std::uint8_t {
    Ingest = 0,   ///< Задача записи (высокий приоритет).
    History = 1   ///< Задача чтения истории (низкий приоритет).
};

/// \brief Хелпер для индексации массивов метрик по JobKind.
/// \param kind Тип задачи.
/// \return Индекс в массиве (0 для Ingest, 1 для History).
inline int to_index(JobKind kind) {
    return static_cast<int>(kind);
}

/// \brief Статус постановки задачи в очередь.
enum class EnqueueStatus {
    Ok,       ///< Успешно добавлена в очередь.
    Rejected, ///< Очередь полна, HTTP должен вернуть 429/503.
    Dropped   ///< Очередь полна, WS должен отправить error frame (БУДУЩЕЕ, не используется в Этапе 3).
};

/// \brief Результат постановки задачи в очередь.
struct EnqueueResult {
    EnqueueStatus status;      ///< Статус операции.
    std::string error_code;    ///< Код ошибки ("overload.ingest_queue_full", "overload.history_queue_full").
    std::string error_message; ///< Человекочитаемое описание ошибки.
};

/// \brief Задача для обработки в WorkerPool.
///
/// КРИТИЧНО: payload guidelines (для code review)
/// - ТОЛЬКО move-объекты или smart pointers: [dto = std::move(unique_ptr), promise = shared_ptr]
/// - ЗАПРЕЩЕНО: захват больших объектов по значению (дорогое копирование в std::function)
/// - ЗАПРЕЩЕНО: capture by reference на stack-объекты ([&local_var] -> UB после enqueue)
/// - ОБЯЗАТЕЛЬНО: DTO передавать через unique_ptr<IngestRequestDTO> в capture
struct Job {
    JobKind kind;                    ///< Тип задачи (Ingest/History).
    std::string request_id;          ///< ID для логирования/трейсинга.
    std::uint64_t enqueue_ts_ms;     ///< Timestamp постановки в очередь (steady_clock! НЕ system_clock).
    std::function<void()> payload;   ///< Исполняемая логика задачи.
};

} // namespace dfh_node
