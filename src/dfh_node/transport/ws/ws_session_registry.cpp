/// \file ws_session_registry.cpp
/// \brief Реализация реестра WS-сессий.
/// \details Реализует регистрацию/удаление соединений, обратный lookup по raw
/// указателю и хранение pending-состояния для `dfhbin` протокола.
///
#include "ws_session_registry.hpp"

#include <openssl/crypto.h>

#include <utility>

namespace dfh_node::transport {

WsConnectionContext::~WsConnectionContext() { OPENSSL_cleanse(signing_key, sizeof(signing_key)); }

WsSessionRegistry::ConnectionId WsSessionRegistry::register_connection(std::shared_ptr<SwsConnection> conn,
                                                                       WsConnectionContext ctx) {
    const std::uint64_t next = m_counter.fetch_add(1);
    ConnectionId connection_id = "ws-" + std::to_string(next);
    ctx.connection_id = connection_id;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.emplace(connection_id, Entry{std::weak_ptr<SwsConnection>(conn), std::move(ctx), std::nullopt, false});
    if (conn) {
        const auto ptr_insert = m_ptr_to_id.emplace(conn.get(), connection_id);
        if (!ptr_insert.second) {
            ptr_insert.first->second = connection_id;
        }
    }
    return connection_id;
}

void WsSessionRegistry::unregister_connection(const ConnectionId &id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto entry_it = m_entries.find(id);
    if (entry_it == m_entries.end()) {
        return;
    }

    entry_it->second.closed = true;

    const ConnectionId reverse_id =
        entry_it->second.ctx.connection_id.empty() ? id : entry_it->second.ctx.connection_id;
    for (auto ptr_it = m_ptr_to_id.begin(); ptr_it != m_ptr_to_id.end(); ++ptr_it) {
        if (ptr_it->second == reverse_id) {
            m_ptr_to_id.erase(ptr_it);
            break;
        }
    }

    m_entries.erase(entry_it);
}

std::optional<WsSessionRegistry::ConnectionId>
WsSessionRegistry::find_id(const WsSessionRegistry::SwsConnection *raw) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_ptr_to_id.find(raw);
    if (it == m_ptr_to_id.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::weak_ptr<WsSessionRegistry::SwsConnection> WsSessionRegistry::get_connection(const ConnectionId &id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_entries.find(id);
    if (it == m_entries.end()) {
        return {};
    }
    return it->second.conn_weak;
}

std::optional<WsConnectionContext> WsSessionRegistry::get_context(const ConnectionId &id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_entries.find(id);
    if (it == m_entries.end()) {
        return std::nullopt;
    }
    return it->second.ctx;
}

void WsSessionRegistry::set_pending_dfhbin(const ConnectionId &id, PendingDfhbinState state) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_entries.find(id);
    if (it == m_entries.end()) {
        return;
    }
    it->second.pending = std::move(state);
}

void WsSessionRegistry::clear_pending_dfhbin(const ConnectionId &id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_entries.find(id);
    if (it == m_entries.end()) {
        return;
    }
    it->second.pending = std::nullopt;
}

std::optional<PendingDfhbinState> WsSessionRegistry::take_pending_dfhbin(const ConnectionId &id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_entries.find(id);
    if (it == m_entries.end()) {
        return std::nullopt;
    }

    std::optional<PendingDfhbinState> state = std::move(it->second.pending);
    it->second.pending = std::nullopt;
    return state;
}

std::size_t WsSessionRegistry::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

} // namespace dfh_node::transport
