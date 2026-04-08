/// \file test_config_loader.cpp
/// \brief Проверка загрузки конфигурации из файла.
/// \details Покрывает успешные сценарии и ошибки структуры/типов.
///
#include "config.hpp"
#include "security.hpp"
#include "test_helpers.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path make_temp_path(const std::string &file_name) {
    return std::filesystem::temp_directory_path() / file_name;
}

std::filesystem::path write_temp_json(const std::string &file_name, const std::string &json_text) {
    const auto path = make_temp_path(file_name);
    std::ofstream out(path, std::ios::out | std::ios::binary);
    out << json_text;
    return path;
}

void remove_temp_file(const std::filesystem::path &path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

bool has_error(const dfh_node::config::LoadResult &result, const std::string &path, const std::string &code) {
    for (const auto &error : result.errors) {
        if (error.path == path && error.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    {
        // Успешная загрузка минимального конфига.
        auto config_path = std::filesystem::path(__FILE__).parent_path() / ".." / "examples" / "config_minimal.json";
        auto result = dfh_node::config::load_from_file(config_path);
        CHECK(result.is_ok());
        CHECK(result.config.has_value());
        CHECK_EQ(result.config->node_id, "node-01");
        CHECK_EQ(result.config->http.request_timeout_ms, static_cast<std::int64_t>(30000));
        CHECK_EQ(result.config->http.history_max_range_ms, static_cast<std::int64_t>(86400000));
        CHECK_EQ(result.config->http.history_max_bytes, static_cast<std::int64_t>(104857600));
        CHECK_EQ(result.config->ws.request_timeout_ms, static_cast<std::int64_t>(30000));
        CHECK_EQ(result.config->ws.history_max_range_ms, static_cast<std::int64_t>(86400000));
        CHECK_EQ(result.config->ws.history_max_bytes, static_cast<std::int64_t>(104857600));
        CHECK_EQ(result.config->ws.max_ws_connections_total, static_cast<std::int64_t>(1000));
    }

    {
        // Явные WS-лимиты должны переопределяться из файла.
        const auto temp_path = write_temp_json("dfh_node_ws_limits.json", R"({
            "schema_version": 1,
            "node_id": "node-ws-limits",
            "env": "dev",
            "security": {
                "server_secret": "test-secret-key-16chars"
            },
            "ws": {
                "history_max_range_ms": 1234,
                "history_max_bytes": 5678,
                "max_ws_connections_total": 9
            }
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(result.is_ok());
        CHECK(result.config.has_value());
        CHECK_EQ(result.config->ws.history_max_range_ms, static_cast<std::int64_t>(1234));
        CHECK_EQ(result.config->ws.history_max_bytes, static_cast<std::int64_t>(5678));
        CHECK_EQ(result.config->ws.max_ws_connections_total, static_cast<std::int64_t>(9));
        remove_temp_file(temp_path);
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
        const auto temp_path = write_temp_json("dfh_node_invalid_config.json", "{");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(!result.errors.empty());
        CHECK_EQ(result.errors[0].code, "parse_error");
        remove_temp_file(temp_path);
    }

    {
        // Корневой JSON обязан быть объектом.
        const auto temp_path = write_temp_json("dfh_node_root_array.json", "[]");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(has_error(result, "", "type_mismatch"));
        remove_temp_file(temp_path);
    }

    {
        // Пропуск обязательных полей фиксируется отдельными ошибками.
        const auto temp_path = write_temp_json("dfh_node_missing_required.json", R"({
            "schema_version": 1
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(has_error(result, "node_id", "missing"));
        CHECK(has_error(result, "env", "missing"));
        CHECK(has_error(result, "security.server_secret", "missing"));
        remove_temp_file(temp_path);
    }

    {
        // Типы верхнеуровневых секций и required-полей валидируются.
        const auto temp_path = write_temp_json("dfh_node_top_level_types.json", R"({
            "schema_version": "1",
            "node_id": 1,
            "env": false,
            "http": 1,
            "ws": 1,
            "queues": 1,
            "security": "x",
            "auth": 1,
            "sync": 1,
            "storage": 1,
            "logging": 1,
            "peers": 1
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(has_error(result, "schema_version", "type_mismatch"));
        CHECK(has_error(result, "node_id", "type_mismatch"));
        CHECK(has_error(result, "env", "type_mismatch"));
        CHECK(has_error(result, "http", "type_mismatch"));
        CHECK(has_error(result, "ws", "type_mismatch"));
        CHECK(has_error(result, "queues", "type_mismatch"));
        CHECK(has_error(result, "security", "type_mismatch"));
        CHECK(has_error(result, "auth", "type_mismatch"));
        CHECK(has_error(result, "sync", "type_mismatch"));
        CHECK(has_error(result, "storage", "type_mismatch"));
        CHECK(has_error(result, "logging", "type_mismatch"));
        CHECK(has_error(result, "peers", "type_mismatch"));
        remove_temp_file(temp_path);
    }

    {
        // anti_replay должен быть объектом.
        const auto temp_path = write_temp_json("dfh_node_antireplay_type.json", R"({
            "schema_version": 1,
            "node_id": "node-01",
            "env": "dev",
            "security": {
                "server_secret": "test-secret-key-16chars",
                "anti_replay": 1
            }
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(has_error(result, "security.anti_replay", "type_mismatch"));
        remove_temp_file(temp_path);
    }

    {
        // require_for_scopes обязан быть массивом.
        const auto temp_path = write_temp_json("dfh_node_antireplay_scopes_type.json", R"({
            "schema_version": 1,
            "node_id": "node-01",
            "env": "dev",
            "security": {
                "server_secret": "test-secret-key-16chars",
                "anti_replay": {
                    "require_for_scopes": "write"
                }
            }
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(has_error(result, "security.anti_replay.require_for_scopes", "type_mismatch"));
        remove_temp_file(temp_path);
    }

    {
        // Элементы require_for_scopes должны быть строками.
        const auto temp_path = write_temp_json("dfh_node_antireplay_scope_item_type.json", R"({
            "schema_version": 1,
            "node_id": "node-01",
            "env": "dev",
            "security": {
                "server_secret": "test-secret-key-16chars",
                "anti_replay": {
                    "require_for_scopes": ["write", 123]
                }
            }
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(has_error(result, "security.anti_replay.require_for_scopes[1]", "type_mismatch"));
        remove_temp_file(temp_path);
    }

    {
        // Успешный разбор auth.api_keys с преобразованием token -> fingerprint.
        const auto temp_path = write_temp_json("dfh_node_auth_config.json", R"({
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
        })");

        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(result.is_ok());
        CHECK(result.config.has_value());
        CHECK_EQ(result.config->auth.cache_ttl_ms, 111);
        CHECK_EQ(result.config->auth.rps_limit, 222);
        CHECK_EQ(result.config->auth.ws_max_connections, 333);
        CHECK_EQ(result.config->auth.rate_limit_window_ms, 444);
        CHECK_EQ(result.config->auth.api_keys.size(), static_cast<std::size_t>(2));

        const dfh_node::FingerprintComputer computer("secret-key");
        const auto expected_fp_1 = computer.compute("token-1");
        const auto expected_fp_2 = computer.compute("token-2");

        CHECK_EQ(result.config->auth.api_keys[0].fingerprint, expected_fp_1);
        CHECK_EQ(result.config->auth.api_keys[1].fingerprint, expected_fp_2);
        CHECK_EQ(result.config->auth.api_keys[0].scope_mask, dfh_node::Scope::Read | dfh_node::Scope::Write);
        CHECK(dfh_node::has_scope(result.config->auth.api_keys[1].scope_mask, dfh_node::Scope::Admin));
        CHECK_EQ(result.config->auth.api_keys[0].rps_limit, 10);
        CHECK_EQ(result.config->auth.api_keys[0].ws_max_connections, 2);
        CHECK_EQ(result.config->auth.api_keys[1].rps_limit, 222);
        CHECK_EQ(result.config->auth.api_keys[1].ws_max_connections, 333);
        remove_temp_file(temp_path);
    }

    {
        // Пользовательские параметры sync корректно читаются из JSON.
        const auto temp_path = write_temp_json("dfh_node_sync_config.json", R"({
            "schema_version": 1,
            "node_id": "node-sync",
            "env": "dev",
            "security": {
                "server_secret": "test-secret-key-16chars"
            },
            "sync": {
                "enabled": true,
                "pull_interval_ms": 15000,
                "request_timeout_ms": 7000,
                "meta_max_blocks": 333,
                "max_blocks_per_cycle": 222,
                "max_parallel_downloads": 3,
                "outbound_token": "sync-outbound-token"
            },
            "peers": [
                { "id": "node-b", "url": "http://192.168.1.2:8080" }
            ]
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(result.is_ok());
        CHECK(result.config.has_value());
        CHECK(result.config->sync.enabled);
        CHECK_EQ(result.config->sync.pull_interval_ms, static_cast<std::int64_t>(15000));
        CHECK_EQ(result.config->sync.request_timeout_ms, static_cast<std::int64_t>(7000));
        CHECK_EQ(result.config->sync.meta_max_blocks, static_cast<std::int64_t>(333));
        CHECK_EQ(result.config->sync.max_blocks_per_cycle, static_cast<std::int64_t>(222));
        CHECK_EQ(result.config->sync.max_parallel_downloads, 3);
        CHECK_EQ(result.config->sync.outbound_token, "sync-outbound-token");
        CHECK_EQ(result.config->peers.size(), static_cast<std::size_t>(1));
        CHECK_EQ(result.config->peers[0].id, "node-b");
        CHECK_EQ(result.config->peers[0].url, "http://192.168.1.2:8080");
        remove_temp_file(temp_path);
    }

    {
        // anti_replay.require_for_scopes читается из массива строк.
        const auto temp_path = write_temp_json("dfh_node_scopes_config.json", R"({
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
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(result.is_ok());
        CHECK(result.config.has_value());
        CHECK_EQ(result.config->security.anti_replay.require_for_scopes,
                 dfh_node::to_scope_mask(dfh_node::Scope::Write) | dfh_node::to_scope_mask(dfh_node::Scope::Sync));
        remove_temp_file(temp_path);
    }

    {
        // Пользовательский http.request_timeout_ms корректно читается из JSON.
        const auto temp_path = write_temp_json("dfh_node_http_timeout_config.json", R"({
            "schema_version": 1,
            "node_id": "node-http-timeout",
            "env": "dev",
            "security": {
                "server_secret": "test-secret-key-16chars"
            },
            "http": {
                "request_timeout_ms": 12345
            }
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(result.is_ok());
        CHECK(result.config.has_value());
        CHECK_EQ(result.config->http.request_timeout_ms, static_cast<std::int64_t>(12345));
        remove_temp_file(temp_path);
    }

    {
        // Пользовательский ws.request_timeout_ms корректно читается из JSON.
        const auto temp_path = write_temp_json("dfh_node_ws_timeout_config.json", R"({
            "schema_version": 1,
            "node_id": "node-ws-timeout",
            "env": "dev",
            "security": {
                "server_secret": "test-secret-key-16chars"
            },
            "ws": {
                "request_timeout_ms": 54321
            }
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(result.is_ok());
        CHECK(result.config.has_value());
        CHECK_EQ(result.config->ws.request_timeout_ms, static_cast<std::int64_t>(54321));
        remove_temp_file(temp_path);
    }

    {
        // Пользовательские лимиты history корректно читаются из JSON.
        const auto temp_path = write_temp_json("dfh_node_http_history_limits_config.json", R"({
            "schema_version": 1,
            "node_id": "node-http-history-limits",
            "env": "dev",
            "security": {
                "server_secret": "test-secret-key-16chars"
            },
            "http": {
                "history_max_range_ms": 3600000,
                "history_max_bytes": 2048
            }
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(result.is_ok());
        CHECK(result.config.has_value());
        CHECK_EQ(result.config->http.history_max_range_ms, static_cast<std::int64_t>(3600000));
        CHECK_EQ(result.config->http.history_max_bytes, static_cast<std::int64_t>(2048));
        remove_temp_file(temp_path);
    }

    {
        // api_keys должен быть массивом.
        const auto temp_path = write_temp_json("dfh_node_api_keys_type.json", R"({
            "schema_version": 1,
            "node_id": "node-01",
            "env": "dev",
            "security": {
                "server_secret": "test-secret-key-16chars"
            },
            "auth": {
                "api_keys": {}
            }
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(has_error(result, "auth.api_keys", "type_mismatch"));
        remove_temp_file(temp_path);
    }

    {
        // Покрываем негативные сценарии структуры auth.api_keys.
        const auto temp_path = write_temp_json("dfh_node_api_keys_errors.json", R"({
            "schema_version": 1,
            "node_id": "node-01",
            "env": "dev",
            "security": {
                "server_secret": "test-secret-key-16chars"
            },
            "auth": {
                "api_keys": [
                    123,
                    { "token": "ok-token", "scopes": "read" },
                    {},
                    { "token": 123 },
                    {
                        "token": "ok-token-2",
                        "scopes": [5, "unknown"],
                        "expires_at": "bad",
                        "rate_limit": 5
                    },
                    {
                        "token": "ok-token-3",
                        "rate_limit": {
                            "rps": "bad",
                            "ws_max_connections": "bad"
                        }
                    }
                ]
            }
        })");

        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(has_error(result, "auth.api_keys[0]", "type_mismatch"));
        CHECK(has_error(result, "auth.api_keys[1].scopes", "type_mismatch"));
        CHECK(has_error(result, "auth.api_keys[2].token", "missing"));
        CHECK(has_error(result, "auth.api_keys[3].token", "type_mismatch"));
        CHECK(has_error(result, "auth.api_keys[4].scopes[0]", "type_mismatch"));
        CHECK(has_error(result, "auth.api_keys[4].scopes[1]", "invalid_value"));
        CHECK(has_error(result, "auth.api_keys[4].expires_at", "type_mismatch"));
        CHECK(has_error(result, "auth.api_keys[4].rate_limit", "type_mismatch"));
        CHECK(has_error(result, "auth.api_keys[5].rate_limit.rps", "type_mismatch"));
        CHECK(has_error(result, "auth.api_keys[5].rate_limit.ws_max_connections", "type_mismatch"));
        remove_temp_file(temp_path);
    }

    {
        // Типы storage/logging полей валидируются.
        const auto temp_path = write_temp_json("dfh_node_storage_logging_errors.json", R"({
            "schema_version": 1,
            "node_id": "node-01",
            "env": "dev",
            "security": {
                "server_secret": "test-secret-key-16chars"
            },
            "storage": {
                "path": 123,
                "min_free_bytes": "bad"
            },
            "logging": {
                "level": 1,
                "console": "true",
                "file_path": false
            }
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(has_error(result, "storage.path", "type_mismatch"));
        CHECK(has_error(result, "storage.min_free_bytes", "type_mismatch"));
        CHECK(has_error(result, "logging.level", "type_mismatch"));
        CHECK(has_error(result, "logging.console", "type_mismatch"));
        CHECK(has_error(result, "logging.file_path", "type_mismatch"));
        remove_temp_file(temp_path);
    }

    {
        // Типы полей sync валидируются.
        const auto temp_path = write_temp_json("dfh_node_sync_errors.json", R"({
            "schema_version": 1,
            "node_id": "node-01",
            "env": "dev",
            "security": {
                "server_secret": "test-secret-key-16chars"
            },
            "sync": {
                "enabled": "true",
                "pull_interval_ms": "bad",
                "request_timeout_ms": "bad",
                "meta_max_blocks": "bad",
                "max_blocks_per_cycle": "bad",
                "max_parallel_downloads": "bad",
                "outbound_token": 123
            }
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(has_error(result, "sync.enabled", "type_mismatch"));
        CHECK(has_error(result, "sync.pull_interval_ms", "type_mismatch"));
        CHECK(has_error(result, "sync.request_timeout_ms", "type_mismatch"));
        CHECK(has_error(result, "sync.meta_max_blocks", "type_mismatch"));
        CHECK(has_error(result, "sync.max_blocks_per_cycle", "type_mismatch"));
        CHECK(has_error(result, "sync.max_parallel_downloads", "type_mismatch"));
        CHECK(has_error(result, "sync.outbound_token", "type_mismatch"));
        remove_temp_file(temp_path);
    }

    {
        // peers должен быть массивом объектов со строковыми id/url.
        const auto temp_path = write_temp_json("dfh_node_peers_errors.json", R"({
            "schema_version": 1,
            "node_id": "node-01",
            "env": "dev",
            "security": {
                "server_secret": "test-secret-key-16chars"
            },
            "peers": [
                1,
                { "id": 123, "url": 456 },
                { "id": "peer-ok", "url": "https://example.org" }
            ]
        })");
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(has_error(result, "peers[0]", "type_mismatch"));
        CHECK(has_error(result, "peers[1].id", "type_mismatch"));
        CHECK(has_error(result, "peers[1].url", "type_mismatch"));
        remove_temp_file(temp_path);
    }

    return 0;
}
