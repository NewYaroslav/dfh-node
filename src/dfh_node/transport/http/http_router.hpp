/// \file http_router.hpp
/// \brief Роутер HTTP-эндпоинтов поверх Simple-Web-Server.
/// \details Содержит регистрацию маршрутов `/v1/...` и доступ к зависимостям transport-слоя.
///
#pragma once

#include "adapter/dfh_adapter.hpp"
#include "auth/unified_gate.hpp"
#include "config/config.hpp"
#include "config/mdbx_api_key_store.hpp"
#include "core/disk_monitor.hpp"
#include "scheduler/task_scheduler.hpp"

#include <server_http.hpp>

#include <chrono>
#include <memory>

namespace dfh_node::transport {

/// \brief Регистрирует HTTP-маршруты в SWS-сервере.
class HttpRouter {
public:
    /// \brief Конструктор роутера.
    /// \param gate Единый gate авторизации/лимитов.
    /// \param scheduler Планировщик задач high/low.
    /// \param adapter Адаптер хранения DFH.
    /// \param cfg Полная конфигурация ноды (HTTP-лимиты + поля статуса).
    /// \param disk_monitor Монитор диска для write-gate и расширенного `/v1/status`.
    /// \param mdbx_store MDBX-хранилище ключей для статистики `/v1/status`.
    HttpRouter(UnifiedGate &gate, TaskScheduler &scheduler, IDfhAdapter &adapter, const config::Config &cfg,
               DiskMonitor *disk_monitor = nullptr, MdbxApiKeyStore *mdbx_store = nullptr);

    /// \brief Зарегистрировать все HTTP-маршруты в SWS-сервере.
    /// \param server Экземпляр HTTP-сервера Simple-Web-Server.
    void register_all(SimpleWeb::Server<SimpleWeb::HTTP> &server);

private:
    UnifiedGate &m_gate;                                ///< Gate авторизации и anti-replay.
    TaskScheduler &m_scheduler;                         ///< Планировщик задач обработки.
    IDfhAdapter &m_adapter;                             ///< Адаптер доступа к хранилищу.
    const config::Config &m_cfg;                        ///< Полная конфигурация ноды.
    DiskMonitor *m_disk_monitor;                        ///< Монитор диска для write-path и status.
    MdbxApiKeyStore *m_mdbx_store;                      ///< MDBX-хранилище ключей для status-метрик.
    std::shared_ptr<asio::io_service> m_executor;       ///< `io_service` сервера, заполняется в `register_all`.
    std::chrono::steady_clock::time_point m_started_at; ///< Момент запуска роутера для расчёта uptime.
};

} // namespace dfh_node::transport
