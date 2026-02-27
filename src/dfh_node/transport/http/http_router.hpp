/// \file http_router.hpp
/// \brief Контракт регистрации HTTP-маршрутов в Simple-Web-Server.
/// \details Полная реализация маршрутов добавляется на следующем этапе.
///
#pragma once

#include <server_http.hpp>

namespace dfh_node::transport {

/// \brief Интерфейс регистратора HTTP-маршрутов.
class HttpRouter {
public:
    /// \brief Виртуальный деструктор.
    virtual ~HttpRouter() = default;

    /// \brief Регистрирует маршруты и обработчики в SWS-сервере.
    /// \param server Экземпляр HTTP-сервера Simple-Web-Server.
    virtual void register_all(SimpleWeb::Server<SimpleWeb::HTTP> &server) = 0;
};

} // namespace dfh_node::transport
