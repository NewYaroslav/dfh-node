/// \file sync_router.hpp
/// \brief Роутер HTTP sync API для межнодовой синхронизации.
/// \details Выполняет синхронную выдачу метаданных/блоков и operational status
/// после проверки авторизации и anti-replay.
///
#pragma once

#include "adapter/dfh_adapter.hpp"
#include "auth/unified_gate.hpp"
#include "config/config.hpp"
#include "core/disk_monitor.hpp"

#include <server_http.hpp>

#include <memory>

namespace dfh_node {
class PeerSyncService;
}

namespace dfh_node::transport {

/// \brief Регистрирует HTTP-маршруты Sync API.
class SyncRouter {
public:
    using HttpRequest = std::shared_ptr<SimpleWeb::Server<SimpleWeb::HTTP>::Request>;
    using HttpResponse = std::shared_ptr<SimpleWeb::Server<SimpleWeb::HTTP>::Response>;

    /// \brief Создаёт роутер Sync API.
    /// \param gate Единый gate авторизации и anti-replay.
    /// \param adapter Адаптер хранения.
    /// \param cfg Полная конфигурация ноды.
    /// \param sync_service Сервис pull-синхронизации; допускается `nullptr`.
    /// \param disk_monitor Монитор состояния диска; допускается `nullptr`.
    SyncRouter(UnifiedGate &gate, IDfhAdapter &adapter, const config::Config &cfg, PeerSyncService *sync_service,
               DiskMonitor *disk_monitor);

    /// \brief Регистрирует все маршруты Sync API в HTTP-сервере.
    /// \param server Экземпляр SWS HTTP server.
    void register_all(SimpleWeb::Server<SimpleWeb::HTTP> &server);

private:
    /// \brief Обрабатывает `POST /sync/meta`.
    /// \param req HTTP-запрос.
    /// \param resp HTTP-ответ.
    void handle_meta(HttpRequest req, HttpResponse resp);

    /// \brief Обрабатывает `GET /sync/block`.
    /// \param req HTTP-запрос.
    /// \param resp HTTP-ответ.
    void handle_block(HttpRequest req, HttpResponse resp);

    /// \brief Обрабатывает `GET /sync/status`.
    /// \param req HTTP-запрос.
    /// \param resp HTTP-ответ.
    void handle_status(HttpRequest req, HttpResponse resp);

    UnifiedGate &m_gate;
    IDfhAdapter &m_adapter;
    const config::Config &m_cfg;
    PeerSyncService *m_sync_service;
    DiskMonitor *m_disk_monitor;
};

} // namespace dfh_node::transport
