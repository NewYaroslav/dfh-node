/// \file test_nonce_store.cpp
/// \brief Тесты хранилища nonce.
/// \details Проверяет запись, replay, TTL-очистку и LRU-вытеснение.
///
#include "security/nonce_store.hpp"
#include "test_helpers.hpp"

namespace {

void test_check_and_record_first_time_success() {
    MockClock clock(1000);
    dfh_node::NonceStore store(clock, 60000, 10);

    CHECK(store.check_and_record("fp1", "nonce1", 1000));
    CHECK_EQ(store.size(), 1u);
}

void test_check_and_record_replay_detected() {
    MockClock clock(1000);
    dfh_node::NonceStore store(clock, 60000, 10);

    CHECK(store.check_and_record("fp1", "nonce1", 1000));
    CHECK(!store.check_and_record("fp1", "nonce1", 1000));
    CHECK_EQ(store.size(), 1u);
}

void test_cleanup_expired() {
    MockClock clock(1000);
    dfh_node::NonceStore store(clock, 60000, 10);

    CHECK(store.check_and_record("fp1", "nonce1", 1000));
    clock.set_now(70001);
    store.cleanup_expired();
    CHECK_EQ(store.size(), 0u);
}

void test_lru_eviction() {
    MockClock clock(1000);
    dfh_node::NonceStore store(clock, 60000, 2);

    CHECK(store.check_and_record("fp1", "nonce1", 1000));
    CHECK(store.check_and_record("fp1", "nonce2", 1000));
    CHECK(store.check_and_record("fp1", "nonce3", 1000));
    CHECK_EQ(store.size(), 2u);

    // nonce1 должен быть вытеснен как oldest.
    CHECK(store.check_and_record("fp1", "nonce1", 1000));
}

void test_different_fingerprints() {
    MockClock clock(1000);
    dfh_node::NonceStore store(clock, 60000, 10);

    CHECK(store.check_and_record("fp1", "nonce1", 1000));
    CHECK(store.check_and_record("fp2", "nonce1", 1000));
    CHECK_EQ(store.size(), 2u);
}

} // namespace

int main() {
    test_check_and_record_first_time_success();
    test_check_and_record_replay_detected();
    test_cleanup_expired();
    test_lru_eviction();
    test_different_fingerprints();
    return 0;
}
