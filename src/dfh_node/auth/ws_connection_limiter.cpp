/// \file ws_connection_limiter.cpp
/// \brief Реализация лимитера WS-соединений.
/// \details Поддерживает атомарную регистрацию и снятие соединений под
/// мьютексом.
///
#include "ws_connection_limiter.hpp"

namespace dfh_node {

WsConnectionLimiter::WsConnectionLimiter(const std::int64_t max_total) : m_max_total(max_total) {}

bool WsConnectionLimiter::register_connection(const std::string &fingerprint, std::int64_t max_connections) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_max_total > 0 && m_total_connections.load(std::memory_order_relaxed) >= m_max_total) {
        return false;
    }

    auto &count = m_connections[fingerprint];
    if (count >= max_connections) {
        return false;
    }

    ++count;
    m_total_connections.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void WsConnectionLimiter::unregister_connection(const std::string &fingerprint) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_connections.find(fingerprint);
    if (it != m_connections.end() && it->second > 0) {
        --it->second;
        if (it->second == 0) {
            m_connections.erase(it);
        }
        if (m_total_connections.load(std::memory_order_relaxed) > 0) {
            m_total_connections.fetch_sub(1, std::memory_order_relaxed);
        }
    }
}

std::int64_t WsConnectionLimiter::active_connections(const std::string &fingerprint) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_connections.find(fingerprint);
    return (it != m_connections.end()) ? it->second : 0;
}

std::int64_t WsConnectionLimiter::total_active_connections() const {
    return m_total_connections.load(std::memory_order_relaxed);
}

} // namespace dfh_node
