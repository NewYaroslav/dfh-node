/// \file disk_monitor.cpp
/// \brief Реализация мониторинга свободного места на диске.
/// \details Кэширует результат `std::filesystem::space()` на 5 секунд, чтобы
/// не перегружать hot path частыми системными вызовами.
///
#include "disk_monitor.hpp"

#include <filesystem>

namespace dfh_node {

DiskMonitor::DiskMonitor(std::string path, const std::uint64_t min_free_bytes)
    : m_path(std::move(path)), m_min_free_bytes(min_free_bytes) {}

bool DiskMonitor::is_disk_low() {
    const auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(m_cache_mutex);

    if (m_last_check != std::chrono::steady_clock::time_point{} && now - m_last_check < CACHE_TTL) {
        return m_cached_low.load(std::memory_order_acquire);
    }

    const auto space = std::filesystem::space(m_path);
    const bool is_low = space.available < m_min_free_bytes;

    m_cached_free.store(space.available, std::memory_order_release);
    m_cached_low.store(is_low, std::memory_order_release);
    m_last_check = now;
    return is_low;
}

std::uint64_t DiskMonitor::last_free_bytes() const { return m_cached_free.load(std::memory_order_acquire); }

} // namespace dfh_node
