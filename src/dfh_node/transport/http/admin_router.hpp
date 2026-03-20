/// \file admin_router.hpp
/// \brief Роутер HTTP Admin API для управления динамическими API-ключами.
/// \details Выполняет синхронные CRUD-операции поверх `ApiKeyManager` после
/// проверки авторизации и anti-replay.
///
#pragma once

#include "auth/unified_gate.hpp"
#include "config/api_key_manager.hpp"
#include "config/config.hpp"

#include <server_http.hpp>

#include <memory>

namespace dfh_node::transport {

/// \brief Регистрирует HTTP-маршруты Admin API.
class AdminRouter {
public:
    using HttpRequest = std::shared_ptr<SimpleWeb::Server<SimpleWeb::HTTP>::Request>;
    using HttpResponse = std::shared_ptr<SimpleWeb::Server<SimpleWeb::HTTP>::Response>;

    /// \brief Создаёт роутер Admin API.
    /// \param gate Единый gate авторизации и anti-replay.
    /// \param manager Менеджер мутаций и чтения динамических ключей.
    /// \param cfg Полная конфигурация ноды.
    AdminRouter(UnifiedGate &gate, ApiKeyManager &manager, const config::Config &cfg);

    /// \brief Регистрирует все маршруты Admin API в HTTP-сервере.
    /// \param server Экземпляр SWS HTTP server.
    void register_all(SimpleWeb::Server<SimpleWeb::HTTP> &server);

private:
    void handle_list(HttpRequest req, HttpResponse resp);
    void handle_create(HttpRequest req, HttpResponse resp);
    void handle_get(HttpRequest req, HttpResponse resp);
    void handle_update(HttpRequest req, HttpResponse resp);
    void handle_revoke(HttpRequest req, HttpResponse resp);
    void handle_delete(HttpRequest req, HttpResponse resp);

    UnifiedGate &m_gate;
    ApiKeyManager &m_manager;
    const config::Config &m_cfg;
};

} // namespace dfh_node::transport
