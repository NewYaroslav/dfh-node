/// \file task.hpp
/// \brief Модель задач и результаты постановки в очередь.
/// \details Содержит TaskLane/TaskKind, EnqueueResult и правила работы с
/// payload.
///
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace dfh_node {

/// \brief Лейн планировщика (соответствует конкретной очереди).
enum class TaskLane : std::uint8_t {
    High = 0, ///< High-priority очередь.
    Low = 1   ///< Low-priority очередь.
};

/// \brief Хелпер для индексации массивов метрик по TaskLane.
/// \param lane Лейн планировщика.
/// \return Индекс в массиве (0 для High, 1 для Low).
inline int to_index(TaskLane lane) { return static_cast<int>(lane); }

/// \brief Семантический тип задачи (доменная операция).
enum class TaskKind : std::uint8_t {
    Ingest = 0,  ///< Задача записи.
    History = 1, ///< Задача чтения истории.
    Admin = 2    ///< Административная операция без постановки в `TaskScheduler`.
};

/// \brief Статус постановки задачи в очередь.
enum class EnqueueStatus {
    Ok,       ///< Успешно добавлена в очередь.
    Rejected, ///< Очередь полна, HTTP должен вернуть 429/503.
    Dropped   ///< Очередь полна, WS должен отправить error frame (БУДУЩЕЕ, не
              ///< используется в Этапе 3).
};

/// \brief Результат постановки задачи в очередь.
struct EnqueueResult {
    EnqueueStatus status;      ///< Статус операции.
    std::string error_code;    ///< Код ошибки ("overload.high_priority_queue_full",
                               ///< "overload.low_priority_queue_full").
    std::string error_message; ///< Человекочитаемое описание ошибки.
};

/// \brief Задача для обработки в WorkerPool.
///
/// КРИТИЧНО: payload guidelines (для code review)
/// - ТОЛЬКО move-объекты или smart pointers: [dto = std::move(unique_ptr),
/// promise = shared_ptr]
/// - ЗАПРЕЩЕНО: захват больших объектов по значению (дорогое копирование в
/// std::function)
/// - ЗАПРЕЩЕНО: capture by reference на stack-объекты ([&local_var] -> UB после
/// enqueue)
/// - ОБЯЗАТЕЛЬНО: DTO передавать через unique_ptr<IngestRequestDTO> в capture
struct Task {
    TaskKind kind;                  ///< Тип задачи (Ingest/History/Admin).
    std::string request_id;         ///< ID для логирования/трейсинга.
    std::uint64_t enqueue_ts_ms;    ///< Timestamp постановки в очередь
                                    ///< (steady_clock! НЕ system_clock).
    std::function<void()> payload;  ///< Исполняемая логика задачи.
    TaskLane lane = TaskLane::High; ///< Лейн планировщика (High/Low).
};

} // namespace dfh_node
