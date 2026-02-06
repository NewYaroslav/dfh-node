/**
 * \file test_config_defaults.cpp
 * \brief Проверка значений конфигурации по умолчанию.
 * \details Убеждается, что дефолты не меняются без явной правки.
 */
#include "config.hpp"
#include "test_helpers.hpp"

int main() {
    // Дефолты должны совпадать с ожиданиями в документации/валидаторе.
    auto cfg = dfh_node::config::default_config();
    CHECK_EQ(cfg.schema_version, 1);
    CHECK_EQ(cfg.http.port, 8080);
    CHECK_EQ(cfg.ws.port, 8081);
    CHECK(cfg.node_id.empty());
    CHECK(cfg.env.empty());
    CHECK(cfg.security.server_secret.empty());
    return 0;
}
