/// \file rate_limiter.hpp
/// \brief Лимитер запросов по скользящему окну с ленивой очисткой устаревших записей.
/// \details Хранит состояние по `fingerprint` и использует `steady_clock` для
/// стабильного времени.

#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace dfh_node {

/// \brief Состояние лимита запросов для одного `fingerprint`.
struct RateLimitState {
    std::deque<std::int64_t> timestamps; ///< Метки времени запросов в рамках окна.
};

/// \brief Потокобезопасный лимитер с режимом скользящего окна и периодической
/// очисткой.
class RateLimiter {
public:
    /// \brief Конструктор.
    /// \param default_rps Базовый лимит запросов в секунду.
    /// \param window_ms Размер окна в миллисекундах (`steady_clock`).
    RateLimiter(std::int64_t default_rps, std::int64_t window_ms);

    /// \brief Проверяет лимит и записывает текущий запрос.
    /// \param fingerprint Идентификатор клиента.
    /// \param limit Индивидуальный лимит клиента (если <=0, используется
    /// `default_rps`).
    /// \return true, если запрос разрешён; false, если лимит превышен.
    bool check_and_record(const std::string &fingerprint, std::int64_t limit);

private:
    std::int64_t m_default_rps;
    std::int64_t m_window_ms;
    std::unordered_map<std::string, RateLimitState> m_states;
    mutable std::mutex m_mutex;
    std::atomic<std::uint64_t>     m_operation_count{0};
    static constexpr std::uint64_t k_cleanup_interval = 1000;

    std::int64_t steady_clock_ms() const;
    void cleanup_old_entries_locked();
};

} // namespace dfh_node
