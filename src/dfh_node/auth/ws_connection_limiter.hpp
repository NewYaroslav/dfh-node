/// \file ws_connection_limiter.hpp
/// \brief Лимитер количества WebSocket-соединений на fingerprint.
/// \details Хранит счётчики активных соединений и ограничивает их по заданному
/// лимиту.
///
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace dfh_node {

/// \brief Потокобезопасный лимитер активных WS-соединений по fingerprint.
class WsConnectionLimiter {
public:
    /// \brief Создаёт лимитер WS-соединений.
    /// \param max_total Глобальный лимит активных соединений для всех fingerprint.
    explicit WsConnectionLimiter(std::int64_t max_total = 0);

    /// \brief Регистрирует новое соединение.
    /// \param fingerprint Идентификатор клиента.
    /// \param max_connections Максимально допустимое число соединений для
    /// данного fingerprint.
    /// \return true, если соединение разрешено; false, если лимит превышен.
    bool register_connection(const std::string &fingerprint, std::int64_t max_connections);

    /// \brief Снимает регистрацию соединения.
    /// \param fingerprint Идентификатор клиента.
    void unregister_connection(const std::string &fingerprint);

    /// \brief Возвращает текущее число активных соединений.
    /// \param fingerprint Идентификатор клиента.
    /// \return Количество активных соединений.
    std::int64_t active_connections(const std::string &fingerprint) const;

    /// \brief Возвращает общее число активных WS-соединений.
    /// \return Количество активных соединений по всем fingerprint.
    std::int64_t total_active_connections() const;

private:
    const std::int64_t m_max_total;                   ///< Глобальный лимит активных соединений.
    std::atomic<std::int64_t> m_total_connections{0}; ///< Общее число активных WS-соединений.
    std::unordered_map<std::string, std::int64_t> m_connections;
    mutable std::mutex m_mutex;
};

} // namespace dfh_node
