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

int main() {
    test_basic_limit();
    test_unregister();
    return 0;
}
