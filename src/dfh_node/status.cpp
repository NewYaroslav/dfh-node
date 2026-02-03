/**
 * @file status.cpp
 * @brief Реализации системных источников времени и случайности.
 * @details Использует std::chrono и std::random_device без хранения состояния.
 */
#include "dfh_node/interfaces.hpp"

#include <chrono>
#include <random>

namespace dfh_node {

std::uint64_t SystemClock::now_ms() const {
    using namespace std::chrono;
    // Используем монотонные часы для стабильного измерения аптайма.
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
            .count());
}

std::uint64_t SystemRandom::next_uint64() {
    std::random_device rd;
    // Склеиваем два 32-битных значения для полного 64-битного результата.
    const std::uint64_t hi = static_cast<std::uint64_t>(rd());
    const std::uint64_t lo = static_cast<std::uint64_t>(rd());
    return (hi << 32) ^ lo;
}

} // namespace dfh_node
