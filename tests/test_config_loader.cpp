/**
 * \file test_config_loader.cpp
 * \brief Проверка загрузки конфигурации из файла.
 * \details Тестирует успешный путь, отсутствие файла и ошибку парсинга.
 */
#include "config_loader.hpp"
#include "fingerprint_computer.hpp"
#include "test_helpers.hpp"

#include <filesystem>
#include <fstream>

int main() {
    {
        // Успешная загрузка минимального конфига.
        auto config_path = std::filesystem::path(__FILE__).parent_path() /
                           ".." / "examples" / "config_minimal.json";
        auto result = dfh_node::config::load_from_file(config_path);
        CHECK(result.is_ok());
        CHECK(result.config.has_value());
        CHECK_EQ(result.config->node_id, "node-01");
    }

    {
        // Ошибка при отсутствии файла.
        auto result = dfh_node::config::load_from_file("missing_config.json");
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(!result.errors.empty());
        CHECK_EQ(result.errors[0].code, "file_not_found");
    }

    {
        // Ошибка парсинга JSON.
        auto temp_path = std::filesystem::temp_directory_path() /
                         "dfh_node_invalid_config.json";
        {
            std::ofstream out(temp_path, std::ios::out | std::ios::binary);
            out << "{";
        }
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(!result.errors.empty());
        CHECK_EQ(result.errors[0].code, "parse_error");
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
    }

    {
        // Успешный разбор auth.api_keys с преобразованием token -> fingerprint.
        auto temp_path = std::filesystem::temp_directory_path() /
                         "dfh_node_auth_config.json";
        {
            std::ofstream out(temp_path, std::ios::out | std::ios::binary);
            out << R"({
                "schema_version": 1,
                "node_id": "node-auth",
                "env": "dev",
                "security": {
                    "server_secret": "secret-key"
                },
                "auth": {
                    "cache_ttl_ms": 111,
                    "rps_limit": 222,
                    "ws_max_connections": 333,
                    "rate_limit_window_ms": 444,
                    "api_keys": [
                        {
                            "token": "token-1",
                            "scopes": ["read", "write"],
                            "rate_limit": {
                                "rps": 10,
                                "ws_max_connections": 2
                            }
                        },
                        {
                            "token": "token-2",
                            "scopes": ["admin"]
                        }
                    ]
                }
            })";
        }

        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(result.is_ok());
        CHECK(result.config.has_value());
        CHECK_EQ(result.config->auth.cache_ttl_ms, 111);
        CHECK_EQ(result.config->auth.rps_limit, 222);
        CHECK_EQ(result.config->auth.ws_max_connections, 333);
        CHECK_EQ(result.config->auth.rate_limit_window_ms, 444);
        CHECK_EQ(result.config->auth.api_keys.size(),
                 static_cast<std::size_t>(2));

        const dfh_node::FingerprintComputer computer("secret-key");
        const auto expected_fp_1 = computer.compute("token-1");
        const auto expected_fp_2 = computer.compute("token-2");

        CHECK_EQ(result.config->auth.api_keys[0].fingerprint, expected_fp_1);
        CHECK_EQ(result.config->auth.api_keys[1].fingerprint, expected_fp_2);
        CHECK_EQ(result.config->auth.api_keys[0].scope_mask,
                 dfh_node::Scope::Read | dfh_node::Scope::Write);
        CHECK(dfh_node::has_scope(result.config->auth.api_keys[1].scope_mask,
                                  dfh_node::Scope::Admin));
        CHECK_EQ(result.config->auth.api_keys[0].rps_limit, 10);
        CHECK_EQ(result.config->auth.api_keys[0].ws_max_connections, 2);
        CHECK_EQ(result.config->auth.api_keys[1].rps_limit, 222);
        CHECK_EQ(result.config->auth.api_keys[1].ws_max_connections, 333);
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
    }

    {
        // anti_replay.require_for_scopes читается из массива строк.
        auto temp_path = std::filesystem::temp_directory_path() /
                         "dfh_node_scopes_config.json";
        {
            std::ofstream out(temp_path, std::ios::out | std::ios::binary);
            out << R"({
  "schema_version": 1,
  "node_id": "node-01",
  "env": "dev",
  "security": {
    "server_secret": "test-secret-key-16chars",
    "anti_replay": {
      "enabled": true,
      "max_skew_ms": 5000,
      "nonce_ttl_ms": 60000,
      "nonce_capacity": 10000,
      "require_for_scopes": ["write", "sync", "unknown_scope"]
    }
  }
})";
        }
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(result.is_ok());
        CHECK(result.config.has_value());
        CHECK_EQ(result.config->security.anti_replay.require_for_scopes,
                 dfh_node::to_scope_mask(dfh_node::Scope::Write) |
                 dfh_node::to_scope_mask(dfh_node::Scope::Sync));
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
    }

    return 0;
}
