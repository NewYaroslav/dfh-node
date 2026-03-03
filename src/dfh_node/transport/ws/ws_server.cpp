/// \file ws_server.cpp
/// \brief Реализация обёртки `WsServer`.
/// \details Запускает `SimpleWeb::SocketServer<SimpleWeb::WS>` в выделенном
/// потоке и корректно останавливает сервер через `stop()` + `join()`.
///
#include "ws_server.hpp"

#include <LogIt.hpp>

#include "core/logging.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <utility>

namespace dfh_node::transport {

WsServer::WsServer(const config::WsConfig &cfg, WsRouter &router) : m_cfg(cfg), m_router(router) {
    m_server.config.address = m_cfg.bind_host;
    m_server.config.port = static_cast<unsigned short>(std::max(0, m_cfg.port));
    if (m_cfg.max_payload_bytes > 0) {
        m_server.config.max_message_size = static_cast<std::size_t>(m_cfg.max_payload_bytes);
    }
    if (m_cfg.request_timeout_ms > 0) {
        const auto timeout_seconds =
            static_cast<long>(std::max<std::int64_t>(1, m_cfg.request_timeout_ms / std::int64_t{1000}));
        m_server.config.timeout_request = timeout_seconds;
    }
}

void WsServer::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_started) {
        return;
    }

    m_router.register_all(m_server);
    m_thread = std::thread([this]() {
        try {
            m_server.start();
        } catch (const std::exception &ex) {
            DFH_PRINTF_ERROR("WsServer thread exception: %s", ex.what());
        } catch (...) {
            DFH_ERROR("WsServer thread unknown exception");
        }
    });

    m_started = true;
}

void WsServer::shutdown() {
    std::thread thread_to_join;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_started) {
            return;
        }

        m_server.stop();
        thread_to_join = std::move(m_thread);
        m_started = false;
    }

    if (thread_to_join.joinable()) {
        thread_to_join.join();
    }
}

} // namespace dfh_node::transport
