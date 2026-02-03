/**
 * @file test_config_validator.cpp
 * @brief Проверка правил валидации конфигурации.
 * @details Покрывает валидный случай и типовые ошибки.
 */
#include "dfh_node/config_validator.hpp"
#include "test_helpers.hpp"

int main() {
    {
        // Корректная конфигурация не должна возвращать ошибок.
        auto cfg = dfh_node::config::default_config();
        cfg.node_id = "node-01";
        cfg.env = "dev";
        cfg.security.server_secret = "test-secret-key-16chars";
        auto errors = dfh_node::config::validate(cfg);
        CHECK(errors.empty());
    }

    {
        // Некорректный порт.
        auto cfg = dfh_node::config::default_config();
        cfg.node_id = "node-01";
        cfg.env = "dev";
        cfg.security.server_secret = "test-secret-key-16chars";
        cfg.http.port = 0;
        auto errors = dfh_node::config::validate(cfg);
        CHECK(!errors.empty());
        CHECK_EQ(errors[0].code, "out_of_range");
    }

    {
        // Пустой node_id должен валидироваться как ошибка.
        auto cfg = dfh_node::config::default_config();
        cfg.env = "dev";
        cfg.security.server_secret = "test-secret-key-16chars";
        cfg.node_id = "";
        auto errors = dfh_node::config::validate(cfg);
        CHECK(!errors.empty());
        CHECK_EQ(errors[0].code, "missing");
    }

    return 0;
}
