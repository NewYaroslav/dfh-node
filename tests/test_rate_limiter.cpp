/// \file test_rate_limiter.cpp
/// \brief Юнит-тесты для RateLimiter.
/// \details Покрывает базовый лимит, сдвиг окна и периодическую очистку.
///
#include "rate_limiter.hpp"
#include "test_helpers.hpp"

#include <chrono>
#include <string>
#include <thread>

using namespace dfh_node;

void test_basic_limit() {
    RateLimiter limiter(2, 1000);

    CHECK(limiter.check_and_record("fp1", 2));
    CHECK(limiter.check_and_record("fp1", 2));
    CHECK(!limiter.check_and_record("fp1", 2));
}

void test_window_slide() {
    RateLimiter limiter(2, 100);

    CHECK(limiter.check_and_record("fp1", 2));
    CHECK(limiter.check_and_record("fp1", 2));

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    CHECK(limiter.check_and_record("fp1", 2));
}

void test_opportunistic_cleanup() {
    RateLimiter limiter(10, 100);

    for (int index = 0; index < 10000; ++index) {
        const std::string fp = "fp" + std::to_string(index % 100);
        limiter.check_and_record(fp, 10);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Косвенная проверка: после старения окна лимит снова пропускает запрос.
    CHECK(limiter.check_and_record("fp1", 10));
}

int main() {
    test_basic_limit();
    test_window_slide();
    test_opportunistic_cleanup();
    return 0;
}
