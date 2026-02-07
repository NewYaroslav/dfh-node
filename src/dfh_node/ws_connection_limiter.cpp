/**
 * \file ws_connection_limiter.cpp
 * \brief Реализация лимитера WS-соединений.
 * \details Поддерживает атомарную регистрацию и снятие соединений под
 * мьютексом.
 */
#include "ws_connection_limiter.hpp"

namespace dfh_node {

bool WsConnectionLimiter::register_connection(const std::string &fingerprint,
                                              std::int64_t max_connections) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto &count = m_connections[fingerprint];
    if (count >= max_connections) {
        return false;
    }
    ++count;
    return true;
}

void WsConnectionLimiter::unregister_connection(
    const std::string &fingerprint) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_connections.find(fingerprint);
    if (it != m_connections.end() && it->second > 0) {
        --it->second;
        if (it->second == 0) {
            m_connections.erase(it);
        }
    }
}

std::int64_t
WsConnectionLimiter::active_connections(const std::string &fingerprint) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_connections.find(fingerprint);
    return (it != m_connections.end()) ? it->second : 0;
}

} // namespace dfh_node
