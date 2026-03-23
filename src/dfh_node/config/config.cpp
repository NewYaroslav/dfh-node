/// \file config.cpp
/// \brief Реализация фабрики дефолтной конфигурации.
/// \details Значения соответствуют минимальному рабочему профилю.
///
#include "config.hpp"

namespace dfh_node::config {

Config default_config() {
    // Собираем значения в одном месте, чтобы loader мог переопределять их.
    Config cfg;
    cfg.schema_version = 1;
    cfg.node_id = "";
    cfg.env = "";

    cfg.http.bind_host = "0.0.0.0";
    cfg.http.port = 8080;
    cfg.http.max_payload_bytes = 10000000;
    cfg.http.request_timeout_ms = 30000;
    cfg.http.history_max_range_ms = 86400000;
    cfg.http.history_max_bytes = 104857600;

    cfg.ws.bind_host = "0.0.0.0";
    cfg.ws.port = 8081;
    cfg.ws.max_payload_bytes = 10000000;
    cfg.ws.request_timeout_ms = 30000;

    cfg.queues.high_capacity = 10000;
    cfg.queues.low_capacity = 5000;
    cfg.queues.workers = 4;

    cfg.security.server_secret = "";
    cfg.security.anti_replay.enabled = true;
    cfg.security.anti_replay.max_skew_ms = 5000;
    cfg.security.anti_replay.nonce_ttl_ms = 60000;
    cfg.security.anti_replay.nonce_capacity = 10000;
    cfg.security.anti_replay.require_for_scopes =
        to_scope_mask(Scope::Write) | to_scope_mask(Scope::Admin) | to_scope_mask(Scope::Sync);

    cfg.sync.enabled = false;
    cfg.sync.pull_interval_ms = 60000;
    cfg.sync.request_timeout_ms = 30000;
    cfg.sync.meta_max_blocks = 10000;
    cfg.sync.max_blocks_per_cycle = 1000;
    cfg.sync.max_parallel_downloads = 4;

    cfg.storage.path = "./data";
    cfg.storage.min_free_bytes = 2000000000;

    cfg.logging.level = "info";
    cfg.logging.console = true;
    cfg.logging.file_path = "";

    return cfg;
}

} // namespace dfh_node::config
