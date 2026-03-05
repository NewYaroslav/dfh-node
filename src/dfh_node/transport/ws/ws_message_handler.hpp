/// \file ws_message_handler.hpp
/// \brief Обработчик входящих WS-сообщений и двушагового `dfhbin` конвейера.
/// \details Реализует полный конвейер обработки для control-сообщений и binary frame.
///
#pragma once

#include "adapter/dfh_adapter.hpp"
#include "auth/unified_gate.hpp"
#include "config/config.hpp"
#include "scheduler/task_scheduler.hpp"
#include "ws_protocol.hpp"
#include "ws_session_registry.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dfh_node::transport {

/// \brief Обработчик входящих WS-сообщений.
/// \details Выполняет валидацию control-сообщений, `gate`-проверки,
/// постановку задач в `TaskScheduler` и отправку ответов клиенту.
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
    /// \brief Обработать `op=ingest` для структурированного `payload`.
    /// \param conn_id Идентификатор соединения.
    /// \param msg Контрольное сообщение.
    /// \param is_msgpack Формат endpoint для ответа.
    void handle_ingest(const std::string &conn_id, const WsControlMessage &msg, bool is_msgpack);

    /// \brief Обработать `op=history`.
    /// \param conn_id Идентификатор соединения.
    /// \param msg Контрольное сообщение.
    /// \param is_msgpack Формат endpoint для ответа.
    void handle_history(const std::string &conn_id, const WsControlMessage &msg, bool is_msgpack);

    /// \brief Обработать этап control-сообщения протокола `dfhbin`.
    /// \param conn_id Идентификатор соединения.
    /// \param msg Контрольное сообщение.
    void handle_dfhbin_control(const std::string &conn_id, const WsControlMessage &msg);

    /// \brief Ответить ошибкой на неподдерживаемую `op=subscribe`.
    /// \param conn_id Идентификатор соединения.
    /// \param msg_id Идентификатор исходного сообщения.
    /// \param is_msgpack Формат endpoint для ответа.
    void handle_subscribe(const std::string &conn_id, const std::string &msg_id, bool is_msgpack);

    /// \brief Отправить ответ клиенту по `connection_id`.
    /// \param conn_id Идентификатор соединения.
    /// \param resp Сообщение ответа.
    /// \param is_msgpack Формат endpoint для сериализации.
    void send_response(const std::string &conn_id, const WsResponseMessage &resp, bool is_msgpack);

    /// \brief Отправить ошибку перегруза очереди.
    /// \param conn_id Идентификатор соединения.
    /// \param msg_id Идентификатор исходного сообщения.
    /// \param error_code Код ошибки очереди (`overload.*`).
    /// \param is_msgpack Формат endpoint для сериализации.
    void handle_overload(const std::string &conn_id, const std::string &msg_id, const std::string &error_code,
                         bool is_msgpack);

    /// \brief Определить формат endpoint по контексту сессии.
    /// \param conn_id Идентификатор соединения.
    /// \return `true` для `/ws/msgpack`, иначе `false`.
    bool is_msgpack_connection(const std::string &conn_id) const;

    UnifiedGate &m_gate;
    TaskScheduler &m_scheduler;
    IDfhAdapter &m_adapter;
    const config::Config &m_cfg;
    std::shared_ptr<WsSessionRegistry> m_registry;
};

} // namespace dfh_node::transport
