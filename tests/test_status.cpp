/// \file test_status.cpp
/// \brief Unit-тесты системных реализаций времени и случайности.
/// \details Проверяет базовые инварианты SystemClock и SystemRandom.
///
#include "interfaces.hpp"
#include "test_helpers.hpp"

#include <chrono>
#include <thread>
#include <type_traits>

using namespace dfh_node;

namespace {

void test_system_clock_is_non_decreasing() {
    SystemClock clock;

    const auto first = clock.now_ms();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto second = clock.now_ms();

    CHECK(second >= first);
}

void test_system_random_has_expected_return_type_and_calls() {
    SystemRandom random;
    static_assert(std::is_same_v<decltype(random.next_uint64()), std::uint64_t>, "SystemRandom must return uint64_t");

    const auto value1 = random.next_uint64();
    const auto value2 = random.next_uint64();

    // Проверяем, что получили не только нулевые значения подряд.
    CHECK((value1 != 0u) || (value2 != 0u));
}

} // namespace

int main() {
    test_system_clock_is_non_decreasing();
    test_system_random_has_expected_return_type_and_calls();
    return 0;
}
