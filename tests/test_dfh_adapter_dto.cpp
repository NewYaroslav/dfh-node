/// \file test_dfh_adapter_dto.cpp
/// \brief Юнит-тесты DTO-утилит адаптера хранения.
/// \details Проверяет вычисление границ блоков для тиков и M1 баров.
///
#include "adapter/dfh_adapter_dto.hpp"
#include "test_helpers.hpp"

#include <cstdint>

using namespace dfh_node;

void test_block_ts_for_ticks() {
    constexpr std::int64_t hour_ms = 3600LL * 1000;

    const std::int64_t mid_hour = 5 * hour_ms + 15 * 60 * 1000;
    CHECK_EQ(block_ts_for_ticks(mid_hour), 5 * hour_ms);

    const std::int64_t border = 7 * hour_ms;
    CHECK_EQ(block_ts_for_ticks(border), border);

    const std::int64_t negative = -1;
    CHECK_EQ(block_ts_for_ticks(negative), -hour_ms);
}

void test_block_ts_for_m1bars() {
    constexpr std::int64_t day_ms = 86400LL * 1000;

    const std::int64_t mid_day = 3 * day_ms + 12 * 3600 * 1000;
    CHECK_EQ(block_ts_for_m1bars(mid_day), 3 * day_ms);

    const std::int64_t midnight = 10 * day_ms;
    CHECK_EQ(block_ts_for_m1bars(midnight), midnight);

    const std::int64_t negative = -1;
    CHECK_EQ(block_ts_for_m1bars(negative), -day_ms);
}

void test_from_to_block_bounds_ticks() {
    constexpr std::int64_t hour_ms = 3600LL * 1000;
    const std::int64_t from_ms = 2 * hour_ms + 30 * 60 * 1000;
    const std::int64_t to_ms = 5 * hour_ms + 10 * 60 * 1000;

    const std::int64_t start_block = block_ts_for_ticks(from_ms);
    const std::int64_t end_block = block_ts_for_ticks(to_ms - 1);

    CHECK(block_ts_for_ticks(from_ms) <= start_block);
    CHECK(start_block <= end_block);
    CHECK(end_block <= block_ts_for_ticks(to_ms - 1));
    CHECK_EQ(start_block, 2 * hour_ms);
    CHECK_EQ(end_block, 5 * hour_ms);
}

int main() {
    test_block_ts_for_ticks();
    test_block_ts_for_m1bars();
    test_from_to_block_bounds_ticks();
    return 0;
}
