/// \file http_server.cpp
/// \brief Реализация обёртки HttpServer поверх Simple-Web-Server.
/// \details Использует выделенный поток для accept-loop и обработки io-событий.
///
#include "http_server.hpp"

#include <exception>
#include <iostream>
#include <utility>

namespace dfh_node::transport {

HttpServer::HttpServer(const config::HttpConfig &cfg, HttpRouter &router)
    : m_cfg(cfg), m_router(router), m_server(), m_io_service(std::make_shared<asio::io_service>()) {
    m_server.config.address = m_cfg.bind_host;
    m_server.config.port = static_cast<unsigned short>(m_cfg.port);
    m_server.io_service = m_io_service;
}

void HttpServer::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_started) {
        return;
    }

    m_router.register_all(m_server);
    m_server.bind();

    m_thread = std::thread([this]() {
        try {
            m_server.accept_and_run();
            if (m_io_service->stopped()) {
                m_io_service->reset();
            }
            m_io_service->run();
        } catch (const std::exception &ex) {
            std::clog << "ERROR: HttpServer: exception in server thread: " << ex.what() << '\n';
        } catch (...) {
            std::clog << "ERROR: HttpServer: unknown exception in server thread\n";
        }
    });

    m_started = true;
}

void HttpServer::shutdown() {
    std::thread thread_to_join;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_started) {
            return;
        }

        m_server.stop();
        m_io_service->stop();

        thread_to_join = std::move(m_thread);
        m_started = false;
    }

    if (thread_to_join.joinable()) {
        thread_to_join.join();
    }
}

HttpExecutor HttpServer::get_executor() const { return m_io_service; }

SimpleWeb::Server<SimpleWeb::HTTP> &HttpServer::server() { return m_server; }

} // namespace dfh_node::transport
