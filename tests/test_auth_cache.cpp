/// \file test_auth_cache.cpp
/// \brief Юнит-тесты для AuthCache.
/// \details Покрывает базовый put/get, TTL и периодическую очистку.
///
#include "auth/auth_cache.hpp"
#include "test_helpers.hpp"

#include <chrono>
#include <string>
#include <thread>

using namespace dfh_node;

void test_put_get() {
    AuthCache cache(60000);
    const AuthContext context{"fp1", static_cast<ScopeMask>(Scope::Read), 100, 10, std::nullopt};

    cache.put("fp1", context);
    const auto result = cache.get("fp1");
    CHECK(result.has_value());
    CHECK_EQ(result->fingerprint, "fp1");
}

void test_ttl_expiry() {
    AuthCache cache(100);
    const AuthContext context{"fp1", static_cast<ScopeMask>(Scope::Read), 100, 10, std::nullopt};

    cache.put("fp1", context);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    const auto result = cache.get("fp1");
    CHECK(!result.has_value());
}

void test_opportunistic_cleanup() {
    AuthCache cache(100);

    for (int index = 0; index < 2000; ++index) {
        const std::string fingerprint = "fp" + std::to_string(index);
        const AuthContext context{fingerprint, static_cast<ScopeMask>(Scope::Read), 100, 10, std::nullopt};
        cache.put(fingerprint, context);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    for (int index = 2000; index < 3000; ++index) {
        const std::string fingerprint = "fp" + std::to_string(index);
        const AuthContext context{fingerprint, static_cast<ScopeMask>(Scope::Read), 100, 10, std::nullopt};
        cache.put(fingerprint, context);
    }

    const auto expired = cache.get("fp0");
    CHECK(!expired.has_value());
}

int main() {
    test_put_get();
    test_ttl_expiry();
    test_opportunistic_cleanup();
    return 0;
}
