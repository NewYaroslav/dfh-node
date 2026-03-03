/// \file ws_server.hpp
/// \brief Обёртка над Simple-WebSocket-Server для запуска WS transport-слоя.
/// \details Инкапсулирует жизненный цикл WS-сервера и регистрацию endpoint'ов
/// через `WsRouter`.
///
#pragma once

#include "config/config.hpp"
#include "ws_router.hpp"

#include <server_ws.hpp>

#include <mutex>
#include <thread>

namespace dfh_node::transport {

/// \brief WS-сервер поверх `SimpleWeb::SocketServer<SimpleWeb::WS>`.
class WsServer {
public:
    /// \brief Конструктор.
    /// \param cfg WS-конфигурация (bind/port/лимиты).
    /// \param router Роутер для регистрации endpoint'ов.
    WsServer(const config::WsConfig &cfg, WsRouter &router);

    /// \brief Запускает WS-сервер в отдельном потоке.
    void start();

    /// \brief Останавливает WS-сервер и дожидается завершения рабочего потока.
    void shutdown();

private:
    config::WsConfig m_cfg;
    WsRouter &m_router;
    SimpleWeb::SocketServer<SimpleWeb::WS> m_server;
    std::thread m_thread;
    std::mutex m_mutex;
    bool m_started{false};
};

} // namespace dfh_node::transport
