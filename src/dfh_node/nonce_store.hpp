/// \file nonce_store.hpp
/// \brief Хранилище использованных nonce с TTL и LRU-вытеснением.
/// \details Ключ: (fingerprint, nonce), значение: insertion_time_ms от серверных часов.
///
#pragma once

#include "interfaces.hpp"

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace dfh_node {

/// \brief Хеш-функтор для пары (fingerprint, nonce).
struct NonceKeyHash {
    /// \brief Вычисляет хеш пары строк.
    /// \param key Пара (fingerprint, nonce).
    /// \return Совмещенный hash-значение для unordered-контейнеров.
    std::size_t operator()(const std::pair<std::string, std::string>& key) const;
};

/// \brief Потокобезопасное хранилище nonce с TTL и LRU-вытеснением.
class NonceStore {
public:
    /// \brief Создаёт nonce-хранилище с ограничением времени жизни и ёмкости.
    /// \param clock Источник серверного времени.
    /// \param ttl_ms Время жизни nonce в миллисекундах.
    /// \param capacity Максимум записей; при превышении вытесняется oldest LRU.
    NonceStore(IClock& clock, std::int64_t ttl_ms, std::int64_t capacity);

    /// \brief Проверяет уникальность nonce и записывает новый nonce.
    /// \param fingerprint Fingerprint токена.
    /// \param nonce Значение nonce.
    /// \param insertion_time_ms Время вставки от серверных часов.
    /// \return true, если nonce новый и записан; false при replay.
    bool check_and_record(const std::string& fingerprint, const std::string& nonce, std::int64_t insertion_time_ms);

    /// \brief Удаляет истёкшие записи по правилу TTL.
    void cleanup_expired();

    /// \brief Текущий размер хранилища.
    /// \return Количество записей в store.
    std::size_t size() const;

private:
    using Key = std::pair<std::string, std::string>;

    IClock& m_clock;
    std::int64_t m_ttl_ms;
    std::int64_t m_capacity;
    std::unordered_map<Key, std::int64_t, NonceKeyHash> m_store;
    std::list<Key> m_lru_list;
    mutable std::mutex m_mutex;
    std::uint64_t m_operation_count = 0;

    void evict_oldest();
    void cleanup_expired_locked();
};

} // namespace dfh_node

