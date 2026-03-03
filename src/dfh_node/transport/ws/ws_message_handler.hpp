/// \file ws_message_handler.hpp
/// \brief Заглушка обработчика WS-сообщений до полной реализации конвейера.
/// \details Нужна для связывания `WsRouter` на этапе 4. Полный конвейер
/// (`parse -> gate -> scheduler -> adapter -> response`) реализуется на этапе 5.
///
#pragma once

#include "adapter/dfh_adapter.hpp"
#include "auth/unified_gate.hpp"
#include "config/config.hpp"
#include "scheduler/task_scheduler.hpp"
#include "ws_session_registry.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dfh_node::transport {

/// \brief Обработчик входящих WS-сообщений.
/// \details На текущем этапе методы являются заглушками без бизнес-логики.
class WsMessageHandler {
public:
    /// \brief Создаёт обработчик WS-сообщений.
    /// \param gate Единый gate авторизации/лимитов.
    /// \param scheduler Планировщик задач.
    /// \param adapter Адаптер хранилища DFH.
    /// \param cfg Полная конфигурация ноды.
    /// \param registry Реестр WS-сессий.
    WsMessageHandler(UnifiedGate &gate, TaskScheduler &scheduler, IDfhAdapter &adapter, const config::Config &cfg,
                     std::shared_ptr<WsSessionRegistry> registry);

    /// \brief Обработать текстовое control-message.
    /// \param connection_id Идентификатор WS-соединения.
    /// \param message Текст сообщения.
    /// \param is_msgpack Признак endpoint `msgpack`.
    void handle_text(const std::string &connection_id, const std::string &message, bool is_msgpack);

    /// \brief Обработать бинарный WS-фрейм.
    /// \param connection_id Идентификатор WS-соединения.
    /// \param bytes Сырые байты payload.
    void handle_binary(const std::string &connection_id, const std::vector<std::uint8_t> &bytes);

private:
    UnifiedGate &m_gate;
    TaskScheduler &m_scheduler;
    IDfhAdapter &m_adapter;
    const config::Config &m_cfg;
    std::shared_ptr<WsSessionRegistry> m_registry;
};

} // namespace dfh_node::transport
