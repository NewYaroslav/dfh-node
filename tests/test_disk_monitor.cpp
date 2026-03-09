/// \file test_disk_monitor.cpp
/// \brief Юнит-тесты для DiskMonitor.
/// \details Проверяет базовую логику `disk_low`, обновление кэша и выдачу
/// последнего значения свободного места.
///
#include "core.hpp"
#include "test_helpers.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

namespace {

std::filesystem::path make_temp_dir() {
    const auto name =
        "dfh-node-disk-monitor-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::create_directories(path);
    return path;
}

void test_zero_threshold_is_never_low() {
    const auto path = make_temp_dir();
    dfh_node::DiskMonitor monitor(path.string(), 0);

    CHECK(!monitor.is_disk_low());
}

void test_max_threshold_is_always_low() {
    const auto path = make_temp_dir();
    dfh_node::DiskMonitor monitor(path.string(), std::numeric_limits<std::uint64_t>::max());

    CHECK(monitor.is_disk_low());
}

void test_last_free_bytes_is_populated_after_check() {
    const auto path = make_temp_dir();
    dfh_node::DiskMonitor monitor(path.string(), 0);

    CHECK_EQ(monitor.last_free_bytes(), 0U);
    (void)monitor.is_disk_low();
    CHECK(monitor.last_free_bytes() > 0U);
}

void test_cached_value_is_stable_between_immediate_calls() {
    const auto path = make_temp_dir();
    dfh_node::DiskMonitor monitor(path.string(), 1);

    const bool first = monitor.is_disk_low();
    const std::uint64_t free_after_first = monitor.last_free_bytes();
    const bool second = monitor.is_disk_low();
    const std::uint64_t free_after_second = monitor.last_free_bytes();

    CHECK_EQ(first, second);
    CHECK_EQ(free_after_first, free_after_second);
}

} // namespace

int main() {
    test_zero_threshold_is_never_low();
    test_max_threshold_is_always_low();
    test_last_free_bytes_is_populated_after_check();
    test_cached_value_is_stable_between_immediate_calls();
    return 0;
}
