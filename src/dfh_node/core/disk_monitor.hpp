/// \file disk_monitor.hpp
/// \brief Мониторинг свободного места на диске с коротким кэшем.
/// \details Предоставляет `DiskMonitor` для hot path проверок нехватки места
/// без обращения к `std::filesystem::space()` на каждый запрос.
///
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace dfh_node {

/// \brief Проверяет свободное место для каталога хранилища.
class DiskMonitor {
public:
    /// \brief Создаёт монитор диска.
    /// \param path Путь к каталогу хранилища.
    /// \param min_free_bytes Минимально допустимый объём свободного места.
    explicit DiskMonitor(std::string path, std::uint64_t min_free_bytes);

    /// \brief Проверяет состояние диска с кэшем на 5 секунд.
    /// \return true, если свободного места меньше `min_free_bytes`.
    bool is_disk_low();

    /// \brief Возвращает свободное место из последней проверки.
    /// \return Последнее кэшированное значение свободного места в байтах.
    std::uint64_t last_free_bytes() const;

private:
    std::string m_path;
    std::uint64_t m_min_free_bytes;
    std::atomic<std::uint64_t> m_cached_free{0};
    std::atomic<bool> m_cached_low{false};
    mutable std::mutex m_cache_mutex;
    std::chrono::steady_clock::time_point m_last_check{};
    static constexpr std::chrono::seconds CACHE_TTL{5};
};

} // namespace dfh_node
