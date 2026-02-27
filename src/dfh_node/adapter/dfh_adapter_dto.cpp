/// \file dfh_adapter_dto.cpp
/// \brief Реализация утилит расчёта `block_ts` для DTO адаптера.
/// \details Вычисляет границы блоков с корректным floor-делением для
/// отрицательных меток времени.
///
#include "dfh_adapter_dto.hpp"

namespace dfh_node {

std::int64_t block_ts_for_ticks(std::int64_t ts_ms) {
    constexpr std::int64_t hour_ms = 3600LL * 1000;
    return (ts_ms / hour_ms) * hour_ms - (ts_ms % hour_ms < 0 ? hour_ms : 0);
}

std::int64_t block_ts_for_m1bars(std::int64_t ts_ms) {
    constexpr std::int64_t day_ms = 86400LL * 1000;
    return (ts_ms / day_ms) * day_ms - (ts_ms % day_ms < 0 ? day_ms : 0);
}

} // namespace dfh_node
