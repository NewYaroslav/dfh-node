/// \file http_server.hpp
/// \brief Обёртка над Simple-Web-Server для запуска HTTP-транспорта.
/// \details Инкапсулирует жизненный цикл сервера и предоставляет executor для deferred-reply.
///
#pragma once

#include "config/config.hpp"
#include "http_router.hpp"

#include <memory>
#include <mutex>
#include <thread>

namespace dfh_node::transport {

using HttpExecutor = std::shared_ptr<asio::io_service>;

/// \brief HTTP-сервер поверх Simple-Web-Server.
class HttpServer {
public:
    /// \brief Конструктор.
    /// \param cfg Конфигурация HTTP (bind/port/лимиты).
    /// \param router Роутер, который регистрирует обработчики маршрутов.
    HttpServer(const config::HttpConfig &cfg, HttpRouter &router);

    /// \brief Запускает сервер в отдельном потоке.
    /// \details Маршруты регистрируются перед запуском accept-loop.
    void start();

    /// \brief Останавливает сервер и ожидает завершения рабочего потока.
    void shutdown();

    /// \brief Возвращает `io_service` HTTP-сервера.
    /// \return Shared-pointer на `asio::io_service` для `asio::post`.
    HttpExecutor get_executor() const;

    /// \brief Возвращает внутренний SWS HTTP server для дополнительной регистрации маршрутов.
    /// \details Метод предназначен для bootstrap-кода, который добавляет
    /// независимые роутеры до вызова `start()`.
    /// \return Ссылка на внутренний экземпляр `SimpleWeb::Server<SimpleWeb::HTTP>`.
    SimpleWeb::Server<SimpleWeb::HTTP> &server();

private:
    config::HttpConfig m_cfg;                       ///< Копия HTTP-конфига.
    HttpRouter &m_router;                           ///< Регистратор маршрутов.
    SimpleWeb::Server<SimpleWeb::HTTP> m_server;    ///< Экземпляр SWS HTTP server.
    std::shared_ptr<asio::io_service> m_io_service; ///< `io_service`, которым владеет transport.
    std::thread m_thread;                           ///< Поток accept-loop/io-loop.
    mutable std::mutex m_mutex;                     ///< Синхронизация `start`/`shutdown`.
    bool m_started{false};                          ///< Флаг запущенного сервера.
};

} // namespace dfh_node::transport
