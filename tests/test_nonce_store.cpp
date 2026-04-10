/// \file test_nonce_store.cpp
/// \brief Тесты хранилища nonce.
/// \details Проверяет запись, replay, TTL-очистку и LRU-вытеснение.
///
#include "security.hpp"
#include "test_helpers.hpp"

#include <atomic>
#include <thread>
#include <vector>

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

void test_concurrent_unique_nonce_accepts_all() {
    MockClock clock(1000);
    dfh_node::NonceStore store(clock, 60000, 128);

    constexpr int thread_count = 16;
    std::atomic<int> accepted{0};
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (int index = 0; index < thread_count; ++index) {
        threads.emplace_back([&store, &accepted, index]() {
            const std::string nonce = "nonce" + std::to_string(index);
            if (store.check_and_record("fp1", nonce, 1000)) {
                accepted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto &thread : threads) {
        thread.join();
    }

    CHECK_EQ(accepted.load(std::memory_order_relaxed), thread_count);
    CHECK_EQ(store.size(), static_cast<std::size_t>(thread_count));
}

void test_concurrent_same_nonce_accepts_only_once() {
    MockClock clock(1000);
    dfh_node::NonceStore store(clock, 60000, 128);

    constexpr int thread_count = 16;
    std::atomic<int> accepted{0};
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (int index = 0; index < thread_count; ++index) {
        threads.emplace_back([&store, &accepted]() {
            if (store.check_and_record("fp1", "sharednonce", 1000)) {
                accepted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto &thread : threads) {
        thread.join();
    }

    CHECK_EQ(accepted.load(std::memory_order_relaxed), 1);
    CHECK_EQ(store.size(), 1u);
}

void test_concurrent_insert_and_cleanup_respects_capacity() {
    MockClock clock(1000);
    constexpr std::int64_t capacity = 32;
    dfh_node::NonceStore store(clock, 5, capacity);

    constexpr int insert_thread_count = 4;
    constexpr int inserts_per_thread = 250;
    std::atomic<bool> producers_running{true};
    std::vector<std::thread> threads;
    threads.reserve(insert_thread_count + 1);

    threads.emplace_back([&store, &clock, &producers_running]() {
        for (int index = 0; index < 100; ++index) {
            clock.set_now(1000 + static_cast<std::uint64_t>(index * 10));
            store.cleanup_expired();
            std::this_thread::yield();
        }
        producers_running.store(false, std::memory_order_relaxed);
    });

    for (int thread_index = 0; thread_index < insert_thread_count; ++thread_index) {
        threads.emplace_back([&store, &clock, thread_index]() {
            for (int nonce_index = 0; nonce_index < inserts_per_thread; ++nonce_index) {
                const std::uint64_t now =
                    1000 + static_cast<std::uint64_t>(thread_index * inserts_per_thread + nonce_index);
                clock.set_now(now);
                const std::string nonce = "nonce-" + std::to_string(thread_index) + "-" + std::to_string(nonce_index);
                store.check_and_record("fp1", nonce, static_cast<std::int64_t>(now));
                std::this_thread::yield();
            }
        });
    }

    for (auto &thread : threads) {
        thread.join();
    }

    clock.set_now(100000);
    store.cleanup_expired();
    CHECK(store.size() <= static_cast<std::size_t>(capacity));
    CHECK(!producers_running.load(std::memory_order_relaxed));
}

} // namespace

int main() {
    test_check_and_record_first_time_success();
    test_check_and_record_replay_detected();
    test_cleanup_expired();
    test_lru_eviction();
    test_different_fingerprints();
    test_concurrent_unique_nonce_accepts_all();
    test_concurrent_same_nonce_accepts_only_once();
    test_concurrent_insert_and_cleanup_respects_capacity();
    return 0;
}
