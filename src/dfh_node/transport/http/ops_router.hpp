/// \file ops_router.hpp
/// \brief Роутер эксплуатационных HTTP-endpoint'ов.
/// \details Регистрирует `/health`, `/ready` и `/metrics` для диагностики
/// runtime-состояния ноды.
///
#pragma once

#include "auth/unified_gate.hpp"
#include "config/config.hpp"
#include "config/mdbx_api_key_store.hpp"
#include "core/disk_monitor.hpp"
#include "scheduler/task_scheduler.hpp"
#include "scheduler/worker_pool.hpp"

#include <server_http.hpp>

#include <memory>

namespace dfh_node::transport {

/// \brief Регистрирует HTTP-endpoint'ы эксплуатации и мониторинга.
class OpsRouter {
public:
    using HttpRequest = std::shared_ptr<SimpleWeb::Server<SimpleWeb::HTTP>::Request>;
    using HttpResponse = std::shared_ptr<SimpleWeb::Server<SimpleWeb::HTTP>::Response>;

    /// \brief Создаёт роутер эксплуатационных endpoint'ов.
    /// \param gate Единый gate авторизации и anti-replay.
    /// \param disk_monitor Монитор доступного места на диске.
    /// \param mdbx_store MDBX-хранилище ключей для readiness-проверки.
    /// \param scheduler Планировщик для чтения текущего размера очередей.
    /// \param worker_pool Пул воркеров для метрик обработки и readiness.
    /// \param cfg Полная конфигурация ноды.
    OpsRouter(UnifiedGate &gate, DiskMonitor &disk_monitor, MdbxApiKeyStore &mdbx_store, TaskScheduler &scheduler,
              WorkerPool &worker_pool, const config::Config &cfg);

    /// \brief Регистрирует все эксплуатационные маршруты в HTTP-сервере.
    /// \param server Экземпляр SWS HTTP server.
    void register_all(SimpleWeb::Server<SimpleWeb::HTTP> &server);

private:
    void handle_health(HttpRequest req, HttpResponse resp);
    void handle_ready(HttpRequest req, HttpResponse resp);
    void handle_metrics(HttpRequest req, HttpResponse resp);

    UnifiedGate &m_gate;
    DiskMonitor &m_disk_monitor;
    MdbxApiKeyStore &m_mdbx_store;
    TaskScheduler &m_scheduler;
    WorkerPool &m_worker_pool;
    const config::Config &m_cfg;
};

} // namespace dfh_node::transport
