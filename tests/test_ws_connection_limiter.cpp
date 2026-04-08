/// \file test_ws_connection_limiter.cpp
/// \brief Юнит-тесты для WsConnectionLimiter.
/// \details Проверяет базовый лимит, снятие регистрации и чтение активных
/// соединений.
///
#include "auth.hpp"
#include "test_helpers.hpp"

using namespace dfh_node;

void test_basic_limit() {
    WsConnectionLimiter limiter;

    CHECK(limiter.register_connection("fp1", 2));
    CHECK(limiter.register_connection("fp1", 2));
    CHECK(!limiter.register_connection("fp1", 2));
    CHECK_EQ(limiter.active_connections("fp1"), 2);
}

void test_unregister() {
    WsConnectionLimiter limiter;

    CHECK(limiter.register_connection("fp1", 2));
    CHECK(limiter.register_connection("fp1", 2));
    CHECK_EQ(limiter.active_connections("fp1"), 2);

    limiter.unregister_connection("fp1");
    CHECK_EQ(limiter.active_connections("fp1"), 1);
    CHECK(limiter.register_connection("fp1", 2));
    CHECK_EQ(limiter.active_connections("fp1"), 2);
}

void test_global_limit_and_total_connections() {
    WsConnectionLimiter limiter(3);

    CHECK(limiter.register_connection("fp1", 10));
    CHECK(limiter.register_connection("fp2", 10));
    CHECK(limiter.register_connection("fp3", 10));
    CHECK(!limiter.register_connection("fp4", 10));
    CHECK_EQ(limiter.total_active_connections(), 3);

    limiter.unregister_connection("fp2");
    CHECK_EQ(limiter.total_active_connections(), 2);

    limiter.unregister_connection("fp2");
    limiter.unregister_connection("fp-missing");
    CHECK_EQ(limiter.total_active_connections(), 2);
}

int main() {
    test_basic_limit();
    test_unregister();
    test_global_limit_and_total_connections();
    return 0;
}
