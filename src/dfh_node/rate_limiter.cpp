/// \file rate_limiter.cpp
/// \brief Реализация лимитера запросов по скользящему окну.
/// \details Очищает устаревшие записи каждые 1000 операций без отдельного
/// фонового потока.
///
#include "rate_limiter.hpp"

#include <chrono>

namespace dfh_node {

RateLimiter::RateLimiter(std::int64_t default_rps, std::int64_t window_ms)
    : m_default_rps(default_rps), m_window_ms(window_ms) {}

std::int64_t RateLimiter::steady_clock_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool RateLimiter::check_and_record(const std::string &fingerprint, std::int64_t limit) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::int64_t effective_limit = (limit > 0) ? limit : m_default_rps;
    if (effective_limit <= 0) {
        return false;
    }

    const std::int64_t now_ms = steady_clock_ms();
    auto &state = m_states[fingerprint];

    while (!state.timestamps.empty() && (now_ms - state.timestamps.front()) >= m_window_ms) {
        state.timestamps.pop_front();
    }

    if (state.timestamps.size() >= static_cast<std::size_t>(effective_limit)) {
        return false;
    }

    state.timestamps.push_back(now_ms);

    if (m_operation_count.fetch_add(1, std::memory_order_relaxed) % k_cleanup_interval == 0) {
        cleanup_old_entries_locked();
    }

    return true;
}

void RateLimiter::cleanup_old_entries_locked() {
    const std::int64_t now_ms = steady_clock_ms();
    for (auto it = m_states.begin(); it != m_states.end();) {
        auto &timestamps = it->second.timestamps;
        while (!timestamps.empty() && (now_ms - timestamps.front()) >= m_window_ms) {
            timestamps.pop_front();
        }
        if (timestamps.empty()) {
            it = m_states.erase(it);
            continue;
        }
        ++it;
    }
}

} // namespace dfh_node
