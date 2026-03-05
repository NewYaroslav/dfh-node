/// \file ws_runtime_utils.hpp
/// \brief Вспомогательные утилиты runtime-обработки WS transport-слоя.
/// \details Содержит переиспользуемые функции для формирования `Task`,
/// преобразования `WsOp` и маппинга причин закрытия соединения.
///
#pragma once

#include "auth/unified_gate.hpp"
#include "core/task.hpp"
#include "ws_protocol.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace dfh_node::transport {

/// \brief Вернуть текущее монотонное время в миллисекундах.
/// \return Значение `steady_clock` в ms.
std::uint64_t steady_now_ms();

/// \brief Сформировать `Task` для постановки в `TaskScheduler`.
/// \param kind Семантика операции.
/// \param request_id Идентификатор запроса.
/// \param payload Функция полезной нагрузки.
/// \return Готовый экземпляр `Task`.
Task make_task(TaskKind kind, std::string request_id, std::function<void()> payload);

/// \brief Преобразовать `WsOp` в строковое имя операции.
/// \param op Операция control-message.
/// \return Строка `ingest|history|subscribe`.
std::string ws_op_to_string(WsOp op);

/// \brief Маппинг `GateError` в reason для WS close frame.
/// \param error Ошибка из `UnifiedGate`.
/// \return Причина закрытия в формате протокольного кода.
std::string gate_error_close_reason(const GateError &error);

} // namespace dfh_node::transport
