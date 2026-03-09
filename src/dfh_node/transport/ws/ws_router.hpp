/// \file ws_router.hpp
/// \brief Роутер WS-эндпоинтов поверх `Simple-WebSocket-Server`.
/// \details Регистрирует обработчики `on_open`/`on_message`/`on_close`/`on_error`
/// для endpoint'ов `/ws/json` и `/ws/msgpack`.
///
#pragma once

#include "adapter/dfh_adapter.hpp"
#include "auth/unified_gate.hpp"
#include "config/config.hpp"
#include "core/disk_monitor.hpp"
#include "scheduler/task_scheduler.hpp"
#include "ws_session_registry.hpp"

#include <server_ws.hpp>

#include <memory>
#include <string>

namespace dfh_node::transport {

class WsMessageHandler;

/// \brief Регистрирует WS-эндпоинты и связывает callbacks с логикой transport-слоя.
class WsRouter {
public:
    /// \brief Создаёт роутер WS-эндпоинтов.
    /// \param gate Единый gate авторизации/лимитов.
    /// \param scheduler Планировщик задач.
    /// \param adapter Адаптер хранилища DFH.
    /// \param cfg Полная конфигурация ноды.
    /// \param registry Реестр WS-сессий (копия shared_ptr из bootstrap-кода).
    /// \param disk_monitor Монитор диска для write-path обработчиков.
    WsRouter(UnifiedGate &gate, TaskScheduler &scheduler, IDfhAdapter &adapter, const config::Config &cfg,
             std::shared_ptr<WsSessionRegistry> registry, DiskMonitor *disk_monitor = nullptr);

    /// \brief Зарегистрировать все WS-эндпоинты в SWS-сервере.
    /// \param server Экземпляр `SimpleWeb::SocketServer<SimpleWeb::WS>`.
    void register_all(SimpleWeb::SocketServer<SimpleWeb::WS> &server);

private:
    UnifiedGate &m_gate;
    TaskScheduler &m_scheduler;
    IDfhAdapter &m_adapter;
    const config::Config &m_cfg;
    std::shared_ptr<WsSessionRegistry> m_registry;
    std::shared_ptr<WsMessageHandler> m_handler;
    DiskMonitor *m_disk_monitor;

    /// \brief Зарегистрировать один endpoint.
    /// \param server Экземпляр WS-сервера.
    /// \param path Endpoint-путь (`/ws/json` или `/ws/msgpack`).
    /// \param is_msgpack Признак msgpack endpoint.
    void register_endpoint(SimpleWeb::SocketServer<SimpleWeb::WS> &server, const std::string &path, bool is_msgpack);
};

} // namespace dfh_node::transport
