/// \file test_config_validator.cpp
/// \brief Проверка правил валидации конфигурации.
/// \details Покрывает валидный случай и набор негативных сценариев по полям.
///
#include "config.hpp"
#include "test_helpers.hpp"

#include <string>
#include <vector>

namespace {

bool has_error(const std::vector<dfh_node::config::ValidationError> &errors, const std::string &path,
               const std::string &code) {
    for (const auto &error : errors) {
        if (error.path == path && error.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

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
        // Покрываем большинство правил валидации в одном сценарии.
        auto cfg = dfh_node::config::default_config();
        cfg.schema_version = 2;
        cfg.node_id = "invalid node id";
        cfg.env = "qa";

        cfg.http.port = 0;
        cfg.ws.port = 0;
        cfg.http.bind_host.clear();
        cfg.ws.bind_host.clear();
        cfg.http.max_payload_bytes = 0;
        cfg.http.request_timeout_ms = -1;
        cfg.http.history_max_range_ms = 0;
        cfg.http.history_max_bytes = 0;
        cfg.ws.max_payload_bytes = 0;
        cfg.ws.request_timeout_ms = -1;

        cfg.queues.high_capacity = 0;
        cfg.queues.low_capacity = 0;
        cfg.queues.workers = 0;

        cfg.security.server_secret = "short";
        cfg.security.anti_replay.enabled = true;
        cfg.security.anti_replay.max_skew_ms = 0;
        cfg.security.anti_replay.nonce_ttl_ms = 0;
        cfg.security.anti_replay.nonce_capacity = 0;
        cfg.security.anti_replay.require_for_scopes = 0;

        cfg.storage.path.clear();
        cfg.storage.min_free_bytes = -1;

        cfg.sync.enabled = true;
        cfg.sync.pull_interval_ms = 0;
        cfg.sync.request_timeout_ms = 0;
        cfg.sync.meta_max_blocks = 0;
        cfg.sync.max_blocks_per_cycle = 0;
        cfg.sync.max_parallel_downloads = 0;
        cfg.sync.outbound_token.clear();

        cfg.logging.level = "verbose";

        cfg.peers = {
            {"peer-1", "http://peer-1.local"},
            {"peer-1", "ftp://peer-2.local"},
            {"", ""},
        };

        const auto errors = dfh_node::config::validate(cfg);
        CHECK(!errors.empty());

        CHECK(has_error(errors, "schema_version", "out_of_range"));
        CHECK(has_error(errors, "node_id", "invalid_format"));
        CHECK(has_error(errors, "env", "invalid_format"));
        CHECK(has_error(errors, "http.port", "out_of_range"));
        CHECK(has_error(errors, "ws.port", "out_of_range"));
        CHECK(has_error(errors, "http.port", "conflict"));
        CHECK(has_error(errors, "http.bind_host", "missing"));
        CHECK(has_error(errors, "ws.bind_host", "missing"));
        CHECK(has_error(errors, "http.max_payload_bytes", "out_of_range"));
        CHECK(has_error(errors, "http.request_timeout_ms", "out_of_range"));
        CHECK(has_error(errors, "http.history_max_range_ms", "out_of_range"));
        CHECK(has_error(errors, "http.history_max_bytes", "out_of_range"));
        CHECK(has_error(errors, "ws.max_payload_bytes", "out_of_range"));
        CHECK(has_error(errors, "ws.request_timeout_ms", "out_of_range"));
        CHECK(has_error(errors, "queues.high_capacity", "out_of_range"));
        CHECK(has_error(errors, "queues.low_capacity", "out_of_range"));
        CHECK(has_error(errors, "queues.workers", "out_of_range"));
        CHECK(has_error(errors, "security.server_secret", "out_of_range"));
        CHECK(has_error(errors, "security.anti_replay.max_skew_ms", "out_of_range"));
        CHECK(has_error(errors, "security.anti_replay.nonce_ttl_ms", "out_of_range"));
        CHECK(has_error(errors, "security.anti_replay.nonce_capacity", "out_of_range"));
        CHECK(has_error(errors, "security.anti_replay.require_for_scopes", "missing"));
        CHECK(has_error(errors, "storage.path", "missing"));
        CHECK(has_error(errors, "storage.min_free_bytes", "out_of_range"));
        CHECK(has_error(errors, "sync.pull_interval_ms", "out_of_range"));
        CHECK(has_error(errors, "sync.request_timeout_ms", "out_of_range"));
        CHECK(has_error(errors, "sync.meta_max_blocks", "out_of_range"));
        CHECK(has_error(errors, "sync.max_blocks_per_cycle", "out_of_range"));
        CHECK(has_error(errors, "sync.max_parallel_downloads", "out_of_range"));
        CHECK(has_error(errors, "sync.outbound_token", "missing"));
        CHECK(has_error(errors, "logging.level", "invalid_format"));
        CHECK(has_error(errors, "peers[1].id", "conflict"));
        CHECK(has_error(errors, "peers[1].url", "invalid_format"));
        CHECK(has_error(errors, "peers[2].id", "missing"));
        CHECK(has_error(errors, "peers[2].url", "missing"));
    }

    {
        // Пустой node_id валидируется как missing.
        auto cfg = dfh_node::config::default_config();
        cfg.env = "dev";
        cfg.security.server_secret = "test-secret-key-16chars";
        cfg.node_id.clear();
        const auto errors = dfh_node::config::validate(cfg);
        CHECK(has_error(errors, "node_id", "missing"));
    }

    {
        // node_id длиннее 64 символов валидируется как out_of_range.
        auto cfg = dfh_node::config::default_config();
        cfg.node_id = std::string(65, 'a');
        cfg.env = "dev";
        cfg.security.server_secret = "test-secret-key-16chars";
        const auto errors = dfh_node::config::validate(cfg);
        CHECK(has_error(errors, "node_id", "out_of_range"));
    }

    {
        // Пустой env валидируется отдельно как missing.
        auto cfg = dfh_node::config::default_config();
        cfg.node_id = "node-01";
        cfg.env.clear();
        cfg.security.server_secret = "test-secret-key-16chars";
        const auto errors = dfh_node::config::validate(cfg);
        CHECK(has_error(errors, "env", "missing"));
    }

    {
        // Пустой server_secret валидируется отдельно как missing.
        auto cfg = dfh_node::config::default_config();
        cfg.node_id = "node-01";
        cfg.env = "dev";
        cfg.security.server_secret.clear();
        const auto errors = dfh_node::config::validate(cfg);
        CHECK(has_error(errors, "security.server_secret", "missing"));
    }

    {
        // При выключенном anti_replay нулевые значения не должны давать ошибок anti_replay.
        auto cfg = dfh_node::config::default_config();
        cfg.node_id = "node-01";
        cfg.env = "dev";
        cfg.security.server_secret = "test-secret-key-16chars";
        cfg.security.anti_replay.enabled = false;
        cfg.security.anti_replay.max_skew_ms = 0;
        cfg.security.anti_replay.nonce_ttl_ms = 0;
        cfg.security.anti_replay.nonce_capacity = 0;
        cfg.security.anti_replay.require_for_scopes = 0;

        const auto errors = dfh_node::config::validate(cfg);
        CHECK(!has_error(errors, "security.anti_replay.max_skew_ms", "out_of_range"));
        CHECK(!has_error(errors, "security.anti_replay.nonce_ttl_ms", "out_of_range"));
        CHECK(!has_error(errors, "security.anti_replay.nonce_capacity", "out_of_range"));
        CHECK(!has_error(errors, "security.anti_replay.require_for_scopes", "missing"));
    }

    {
        // При выключенном sync нулевые значения не должны давать ошибок sync.
        auto cfg = dfh_node::config::default_config();
        cfg.node_id = "node-01";
        cfg.env = "dev";
        cfg.security.server_secret = "test-secret-key-16chars";
        cfg.sync.enabled = false;
        cfg.sync.pull_interval_ms = 0;
        cfg.sync.request_timeout_ms = 0;
        cfg.sync.meta_max_blocks = 0;
        cfg.sync.max_blocks_per_cycle = 0;
        cfg.sync.max_parallel_downloads = 0;
        cfg.sync.outbound_token.clear();

        const auto errors = dfh_node::config::validate(cfg);
        CHECK(!has_error(errors, "sync.pull_interval_ms", "out_of_range"));
        CHECK(!has_error(errors, "sync.request_timeout_ms", "out_of_range"));
        CHECK(!has_error(errors, "sync.meta_max_blocks", "out_of_range"));
        CHECK(!has_error(errors, "sync.max_blocks_per_cycle", "out_of_range"));
        CHECK(!has_error(errors, "sync.max_parallel_downloads", "out_of_range"));
        CHECK(!has_error(errors, "sync.outbound_token", "missing"));
    }

    {
        // При включённом sync и настроенных peers outbound_token обязателен.
        auto cfg = dfh_node::config::default_config();
        cfg.node_id = "node-01";
        cfg.env = "dev";
        cfg.security.server_secret = "test-secret-key-16chars";
        cfg.sync.enabled = true;
        cfg.sync.outbound_token.clear();
        cfg.peers = {{"peer-a", "http://peer-a.local"}};

        const auto errors = dfh_node::config::validate(cfg);
        CHECK(has_error(errors, "sync.outbound_token", "missing"));
    }

    {
        // При включённом sync и peers валидный outbound_token снимает ошибку.
        auto cfg = dfh_node::config::default_config();
        cfg.node_id = "node-01";
        cfg.env = "dev";
        cfg.security.server_secret = "test-secret-key-16chars";
        cfg.sync.enabled = true;
        cfg.sync.outbound_token = "sync-outbound-token";
        cfg.peers = {{"peer-a", "http://peer-a.local"}};

        const auto errors = dfh_node::config::validate(cfg);
        CHECK(!has_error(errors, "sync.outbound_token", "missing"));
    }

    {
        // Проверяем ветку warning по capacity-rule: ошибок быть не должно.
        auto cfg = dfh_node::config::default_config();
        cfg.node_id = "node-01";
        cfg.env = "dev";
        cfg.security.server_secret = "test-secret-key-16chars";
        cfg.auth.rps_limit = 100;
        cfg.security.anti_replay.enabled = true;
        cfg.security.anti_replay.max_skew_ms = 5000;
        cfg.security.anti_replay.nonce_ttl_ms = 60000;
        cfg.security.anti_replay.nonce_capacity = 1;
        cfg.security.anti_replay.require_for_scopes =
            dfh_node::to_scope_mask(dfh_node::Scope::Write) | dfh_node::to_scope_mask(dfh_node::Scope::Admin);

        const auto errors = dfh_node::config::validate(cfg);
        CHECK(errors.empty());
    }

    return 0;
}
