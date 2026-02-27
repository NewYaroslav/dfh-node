/// \file http_router.hpp
/// \brief Роутер HTTP-эндпоинтов поверх Simple-Web-Server.
/// \details Содержит регистрацию маршрутов `/v1/...` и доступ к зависимостям transport-слоя.
///
#pragma once

#include "adapter/dfh_adapter.hpp"
#include "auth/unified_gate.hpp"
#include "config/config.hpp"
#include "scheduler/task_scheduler.hpp"

#include <server_http.hpp>

#include <memory>

namespace dfh_node::transport {

/// \brief Регистрирует HTTP-маршруты в SWS-сервере.
class HttpRouter {
public:
    /// \brief Конструктор роутера.
    /// \param gate Единый gate авторизации/лимитов.
    /// \param scheduler Планировщик задач high/low.
    /// \param adapter Адаптер хранения DFH.
    /// \param cfg HTTP-конфигурация (лимиты/таймауты).
    HttpRouter(UnifiedGate &gate, TaskScheduler &scheduler, IDfhAdapter &adapter, const config::HttpConfig &cfg);

    /// \brief Зарегистрировать все HTTP-маршруты в SWS-сервере.
    /// \param server Экземпляр HTTP-сервера Simple-Web-Server.
    void register_all(SimpleWeb::Server<SimpleWeb::HTTP> &server);

private:
    UnifiedGate &m_gate;                          ///< Gate авторизации и anti-replay.
    TaskScheduler &m_scheduler;                   ///< Планировщик задач обработки.
    IDfhAdapter &m_adapter;                       ///< Адаптер доступа к хранилищу.
    const config::HttpConfig &m_cfg;              ///< HTTP-конфигурация.
    std::shared_ptr<asio::io_service> m_executor; ///< `io_service` сервера, заполняется в `register_all`.
};

} // namespace dfh_node::transport
