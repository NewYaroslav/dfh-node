/**
 * \file test_config_validator.cpp
 * \brief Проверка правил валидации конфигурации.
 * \details Покрывает валидный случай и типовые ошибки.
 */
#include "config_validator.hpp"
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

    {
        // При включенном anti_replay маска обязательных scope не должна быть пустой.
        auto cfg = dfh_node::config::default_config();
        cfg.node_id = "node-01";
        cfg.env = "dev";
        cfg.security.server_secret = "test-secret-key-16chars";
        cfg.security.anti_replay.enabled = true;
        cfg.security.anti_replay.require_for_scopes = 0;

        auto errors = dfh_node::config::validate(cfg);
        CHECK(!errors.empty());

        bool found = false;
        for (const auto &error : errors) {
            if (error.path == "security.anti_replay.require_for_scopes") {
                found = true;
                break;
            }
        }
        CHECK(found);
    }

    {
        // При выключенном anti_replay пустая маска допустима.
        auto cfg = dfh_node::config::default_config();
        cfg.node_id = "node-01";
        cfg.env = "dev";
        cfg.security.server_secret = "test-secret-key-16chars";
        cfg.security.anti_replay.enabled = false;
        cfg.security.anti_replay.require_for_scopes = 0;

        auto errors = dfh_node::config::validate(cfg);

        bool found = false;
        for (const auto &error : errors) {
            if (error.path == "security.anti_replay.require_for_scopes") {
                found = true;
                break;
            }
        }
        CHECK(!found);
    }

    return 0;
}
