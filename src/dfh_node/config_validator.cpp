/// \file config_validator.cpp
/// \brief Валидация конфигурации ноды по правилам проекта.
/// \details Формирует список ошибок без прерывания выполнения.
///
#include "config_validator.hpp"

#include <iostream>
#include <regex>
#include <set>

namespace dfh_node::config {
namespace {

// Добавляет новую ошибку в список.
void add_error(std::vector<ValidationError> &errors, const std::string &path, const std::string &code,
               const std::string &message) {
    errors.push_back(ValidationError{path, code, message});
}

// Проверяет допустимые значения env.
bool is_valid_env(const std::string &env) { return env == "dev" || env == "staging" || env == "prod"; }

// Проверяет допустимые уровни логирования.
bool is_valid_level(const std::string &level) {
    return level == "trace" || level == "debug" || level == "info" || level == "warn" || level == "error";
}

// Проверяет, что URL начинается с http(s).
bool starts_with_http(const std::string &url) { return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0; }

} // namespace

std::vector<ValidationError> validate(const Config &cfg) {
    std::vector<ValidationError> errors;

    if (cfg.schema_version != 1) {
        add_error(errors, "schema_version", "out_of_range", "Expected schema_version == 1");
    }

    if (cfg.node_id.empty()) {
        add_error(errors, "node_id", "missing", "node_id must not be empty");
    } else if (cfg.node_id.size() > 64) {
        add_error(errors, "node_id", "out_of_range", "node_id must be <= 64 chars");
    } else {
        // Регулярное выражение фиксирует допустимый набор символов.
        static const std::regex kNodeIdRe("^[A-Za-z0-9_-]+$");
        if (!std::regex_match(cfg.node_id, kNodeIdRe)) {
            add_error(errors, "node_id", "invalid_format", "node_id must match [A-Za-z0-9_-]+");
        }
    }

    if (cfg.env.empty()) {
        add_error(errors, "env", "missing", "env must not be empty");
    } else if (!is_valid_env(cfg.env)) {
        add_error(errors, "env", "invalid_format", "env must be dev|staging|prod");
    }

    if (cfg.http.port < 1 || cfg.http.port > 65535) {
        add_error(errors, "http.port", "out_of_range", "http.port must be 1..65535");
    }
    if (cfg.ws.port < 1 || cfg.ws.port > 65535) {
        add_error(errors, "ws.port", "out_of_range", "ws.port must be 1..65535");
    }
    if (cfg.http.port == cfg.ws.port) {
        add_error(errors, "http.port", "conflict", "http.port and ws.port must differ");
    }
    if (cfg.http.bind_host.empty()) {
        add_error(errors, "http.bind_host", "missing", "http.bind_host must not be empty");
    }
    if (cfg.ws.bind_host.empty()) {
        add_error(errors, "ws.bind_host", "missing", "ws.bind_host must not be empty");
    }

    if (cfg.http.max_payload_bytes <= 0) {
        add_error(errors, "http.max_payload_bytes", "out_of_range", "http.max_payload_bytes must be > 0");
    }
    if (cfg.ws.max_payload_bytes <= 0) {
        add_error(errors, "ws.max_payload_bytes", "out_of_range", "ws.max_payload_bytes must be > 0");
    }

    if (cfg.queues.high_capacity <= 0) {
        add_error(errors, "queues.high_capacity", "out_of_range", "queues.high_capacity must be > 0");
    }
    if (cfg.queues.low_capacity <= 0) {
        add_error(errors, "queues.low_capacity", "out_of_range", "queues.low_capacity must be > 0");
    }
    if (cfg.queues.workers <= 0) {
        add_error(errors, "queues.workers", "out_of_range", "queues.workers must be > 0");
    }

    if (cfg.security.server_secret.empty()) {
        add_error(errors, "security.server_secret", "missing", "security.server_secret must not be empty");
    } else if (cfg.security.server_secret.size() < 16) {
        add_error(errors, "security.server_secret", "out_of_range", "security.server_secret must be >= 16 chars");
    }

    if (cfg.security.anti_replay.enabled) {
        if (cfg.security.anti_replay.max_skew_ms <= 0) {
            add_error(errors, "security.anti_replay.max_skew_ms", "out_of_range", "max_skew_ms must be > 0");
        }
        if (cfg.security.anti_replay.nonce_ttl_ms <= 0) {
            add_error(errors, "security.anti_replay.nonce_ttl_ms", "out_of_range", "nonce_ttl_ms must be > 0");
        }
        if (cfg.security.anti_replay.nonce_capacity <= 0) {
            add_error(errors, "security.anti_replay.nonce_capacity", "out_of_range", "nonce_capacity must be > 0");
        }
        if (cfg.security.anti_replay.require_for_scopes == 0) {
            add_error(errors, "security.anti_replay.require_for_scopes", "missing",
                      "require_for_scopes must not be empty when anti_replay is enabled");
        }

        // Мягкая рекомендация по ёмкости nonce-хранилища.
        // Формула: peak_rps_per_fingerprint * ttl_seconds * 1.5.
        const std::int64_t ttl_seconds = cfg.security.anti_replay.nonce_ttl_ms / 1000;
        const std::int64_t min_capacity = cfg.auth.rps_limit * ttl_seconds * 3 / 2;
        if (cfg.auth.rps_limit > 0 && ttl_seconds > 0 && cfg.security.anti_replay.nonce_capacity < min_capacity) {
            std::clog << "WARN: security.anti_replay.nonce_capacity (" << cfg.security.anti_replay.nonce_capacity
                      << ") < recommended (" << min_capacity << ") for auth.rps_limit * nonce_ttl_seconds * 1.5\n";
        }
    }

    if (cfg.storage.path.empty()) {
        add_error(errors, "storage.path", "missing", "storage.path must not be empty");
    }
    if (cfg.storage.min_free_bytes < 0) {
        add_error(errors, "storage.min_free_bytes", "out_of_range", "storage.min_free_bytes must be >= 0");
    }

    if (!is_valid_level(cfg.logging.level)) {
        add_error(errors, "logging.level", "invalid_format", "logging.level must be trace|debug|info|warn|error");
    }

    std::set<std::string> peer_ids;
    for (std::size_t i = 0; i < cfg.peers.size(); ++i) {
        const auto &peer = cfg.peers[i];
        const std::string id_path = "peers[" + std::to_string(i) + "].id";
        const std::string url_path = "peers[" + std::to_string(i) + "].url";

        if (peer.id.empty()) {
            add_error(errors, id_path, "missing", "peer id must not be empty");
        } else if (!peer_ids.insert(peer.id).second) {
            add_error(errors, id_path, "conflict", "peer id must be unique");
        }

        if (peer.url.empty()) {
            add_error(errors, url_path, "missing", "peer url must not be empty");
        } else if (!starts_with_http(peer.url)) {
            add_error(errors, url_path, "invalid_format", "peer url must start with http:// or https://");
        }
    }

    return errors;
}

} // namespace dfh_node::config
