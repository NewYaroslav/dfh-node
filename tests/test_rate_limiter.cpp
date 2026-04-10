/// \file test_rate_limiter.cpp
/// \brief Юнит-тесты для RateLimiter.
/// \details Покрывает базовый лимит, сдвиг окна и периодическую очистку.
///
#include "auth.hpp"
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

void test_high_load_rejects_roughly_excess_requests() {
    RateLimiter limiter(100, 1000);

    constexpr int total_requests = 1000;
    int accepted = 0;
    for (int index = 0; index < total_requests; ++index) {
        if (limiter.check_and_record("fp-high-load", 100)) {
            ++accepted;
        }
    }

    const int rejected = total_requests - accepted;
    CHECK(rejected >= 810);
    CHECK(rejected <= 990);
}

void test_requests_allowed_after_window_expires() {
    RateLimiter limiter(3, 120);

    CHECK(limiter.check_and_record("fp-window", 3));
    CHECK(limiter.check_and_record("fp-window", 3));
    CHECK(limiter.check_and_record("fp-window", 3));
    CHECK(!limiter.check_and_record("fp-window", 3));

    std::this_thread::sleep_for(std::chrono::milliseconds(160));

    CHECK(limiter.check_and_record("fp-window", 3));
    CHECK(limiter.check_and_record("fp-window", 3));
    CHECK(limiter.check_and_record("fp-window", 3));
}

void test_different_fingerprints_do_not_share_limits() {
    RateLimiter limiter(2, 1000);

    CHECK(limiter.check_and_record("fp-a", 2));
    CHECK(limiter.check_and_record("fp-a", 2));
    CHECK(!limiter.check_and_record("fp-a", 2));

    CHECK(limiter.check_and_record("fp-b", 2));
    CHECK(limiter.check_and_record("fp-b", 2));
    CHECK(!limiter.check_and_record("fp-b", 2));
}

int main() {
    test_basic_limit();
    test_window_slide();
    test_opportunistic_cleanup();
    test_high_load_rejects_roughly_excess_requests();
    test_requests_allowed_after_window_expires();
    test_different_fingerprints_do_not_share_limits();
    return 0;
}
