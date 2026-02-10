/// \file nonce_store.cpp
/// \brief Реализация хранилища использованных nonce.
/// \details Поддерживает TTL-очистку и LRU-вытеснение по capacity.
///
#include "nonce_store.hpp"

#include <functional>

namespace dfh_node {

std::size_t NonceKeyHash::operator()(const std::pair<std::string, std::string>& key) const {
    const std::size_t first_hash = std::hash<std::string>{}(key.first);
    const std::size_t second_hash = std::hash<std::string>{}(key.second);
    return first_hash ^ (second_hash << 1);
}

NonceStore::NonceStore(IClock& clock, std::int64_t ttl_ms, std::int64_t capacity)
    : m_clock(clock), m_ttl_ms(ttl_ms), m_capacity(capacity) {}

bool NonceStore::check_and_record(const std::string& fingerprint, const std::string& nonce,
                                  const std::int64_t insertion_time_ms) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const Key key{fingerprint, nonce};
    if (m_store.find(key) != m_store.end()) {
        return false;
    }

    m_store.emplace(key, insertion_time_ms);
    m_lru_list.push_back(key);

    if (m_capacity > 0 && static_cast<std::int64_t>(m_store.size()) > m_capacity) {
        evict_oldest();
    }

    ++m_operation_count;
    if ((m_operation_count % 1000u) == 0u) {
        cleanup_expired_locked();
    }

    return true;
}

void NonceStore::cleanup_expired() {
    std::lock_guard<std::mutex> lock(m_mutex);
    cleanup_expired_locked();
}

std::size_t NonceStore::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_store.size();
}

void NonceStore::evict_oldest() {
    if (m_lru_list.empty()) {
        return;
    }

    const Key oldest = m_lru_list.front();
    m_lru_list.pop_front();
    m_store.erase(oldest);
}

void NonceStore::cleanup_expired_locked() {
    const std::int64_t now_ms = static_cast<std::int64_t>(m_clock.now_ms());

    auto lru_it = m_lru_list.begin();
    while (lru_it != m_lru_list.end()) {
        auto store_it = m_store.find(*lru_it);
        if (store_it == m_store.end()) {
            lru_it = m_lru_list.erase(lru_it);
            continue;
        }

        if ((now_ms - store_it->second) > m_ttl_ms) {
            m_store.erase(store_it);
            lru_it = m_lru_list.erase(lru_it);
            continue;
        }

        ++lru_it;
    }
}

} // namespace dfh_node

