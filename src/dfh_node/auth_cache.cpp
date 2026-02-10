/**
 * \file auth_cache.cpp
 * \brief Реализация auth-кэша с TTL и периодической очисткой.
 * \details Использует steady_clock для устойчивости к изменениям системного
 * времени.
 */
#include "auth_cache.hpp"

#include <chrono>
#include <mutex>

namespace dfh_node {

AuthCache::AuthCache(std::int64_t ttl_ms) : m_ttl_ms(ttl_ms) {}

std::int64_t AuthCache::steady_clock_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::optional<AuthContext>
AuthCache::get(const std::string &fingerprint) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const auto it = m_cache.find(fingerprint);
    if (it == m_cache.end()) {
        return std::nullopt;
    }

    const std::int64_t now_ms = steady_clock_ms();
    if ((now_ms - it->second.cached_at_ms) > m_ttl_ms) {
        return std::nullopt;
    }
    return it->second.context;
}

void AuthCache::put(const std::string &fingerprint,
                    const AuthContext &context) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    const std::int64_t now_ms = steady_clock_ms();
    m_cache[fingerprint] = CachedAuthContext{context, now_ms, now_ms};

    // Чистим устаревшие записи лениво, чтобы снизить постоянные накладные
    // расходы.
    if (m_operation_count.fetch_add(1, std::memory_order_relaxed) %
            k_cleanup_interval ==
        0) {
        cleanup_expired_locked();
    }
}

void AuthCache::invalidate(const std::string &fingerprint) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_cache.erase(fingerprint);
}

void AuthCache::clear() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_cache.clear();
}

void AuthCache::cleanup_expired_locked() {
    const std::int64_t now_ms = steady_clock_ms();
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if ((now_ms - it->second.cached_at_ms) > m_ttl_ms) {
            it = m_cache.erase(it);
            continue;
        }
        ++it;
    }
}

} // namespace dfh_node
