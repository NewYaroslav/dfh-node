/**
 * \file config_loader.cpp
 * \brief Разбор JSON-конфига и формирование структур Config.
 * \details Ошибки собираются в список, исключения парсинга перехватываются.
 */
#include "config_loader.hpp"

#include "config.hpp"
#include "fingerprint_computer.hpp"

#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>

#include <nlohmann/json.hpp>
#include <openssl/crypto.h>

namespace dfh_node::config {
namespace {

using nlohmann::json;

// Склеивает путь к полю в виде "a.b.c" для сообщений об ошибке.
std::string join_path(const std::string &prefix, const std::string &field) {
    if (prefix.empty()) {
        return field;
    }
    return prefix + "." + field;
}

// Добавляет ошибку загрузки в общий список.
void add_error(std::vector<LoadError> &errors, const std::string &path,
               const std::string &code, const std::string &message) {
    errors.push_back(LoadError{path, code, message});
}

// Считывает строковое поле; при required=true требует наличия поля.
bool read_string(const json &obj, const char *key, std::string &out,
                 std::vector<LoadError> &errors, const std::string &prefix,
                 bool required) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        if (required) {
            add_error(errors, join_path(prefix, key), "missing",
                      "Missing required field");
        }
        return false;
    }
    if (!it->is_string()) {
        add_error(errors, join_path(prefix, key), "type_mismatch",
                  "Expected string");
        return false;
    }
    out = it->get<std::string>();
    return true;
}

// Считывает булево поле; отсутствие поля не считается ошибкой.
bool read_bool(const json &obj, const char *key, bool &out,
               std::vector<LoadError> &errors, const std::string &prefix) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return false;
    }
    if (!it->is_boolean()) {
        add_error(errors, join_path(prefix, key), "type_mismatch",
                  "Expected boolean");
        return false;
    }
    out = it->get<bool>();
    return true;
}

// Считывает int64-значение; отсутствие поля не считается ошибкой.
bool read_int64(const json &obj, const char *key, std::int64_t &out,
                std::vector<LoadError> &errors, const std::string &prefix) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return false;
    }
    if (!it->is_number_integer()) {
        add_error(errors, join_path(prefix, key), "type_mismatch",
                  "Expected integer");
        return false;
    }
    out = it->get<std::int64_t>();
    return true;
}

// Считывает int-значение; отсутствие поля не считается ошибкой.
bool read_int(const json &obj, const char *key, int &out,
              std::vector<LoadError> &errors, const std::string &prefix) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return false;
    }
    if (!it->is_number_integer()) {
        add_error(errors, join_path(prefix, key), "type_mismatch",
                  "Expected integer");
        return false;
    }
    out = it->get<int>();
    return true;
}

} // namespace

LoadResult load_from_file(const std::filesystem::path &path) {
    LoadResult result;

    if (!std::filesystem::exists(path)) {
        // Единый код ошибки для отсутствующего файла.
        add_error(result.errors, path.string(), "file_not_found",
                  "File not found");
        return result;
    }

    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        add_error(result.errors, path.string(), "file_not_found",
                  "File not found");
        return result;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    json root;
    try {
        // Парсим целиком, чтобы получить однозначные сообщения об ошибках.
        root = json::parse(buffer.str());
    } catch (const json::parse_error &ex) {
        add_error(result.errors, path.string(), "parse_error", ex.what());
        return result;
    }

    if (!root.is_object()) {
        add_error(result.errors, "", "type_mismatch", "Expected JSON object");
        return result;
    }

    // Стартуем с дефолтов и затем переопределяем значения из файла.
    Config cfg = default_config();

    read_int(root, "schema_version", cfg.schema_version, result.errors, "");
    read_string(root, "node_id", cfg.node_id, result.errors, "", true);
    read_string(root, "env", cfg.env, result.errors, "", true);

    if (auto it = root.find("http"); it != root.end()) {
        if (!it->is_object()) {
            add_error(result.errors, "http", "type_mismatch",
                      "Expected object");
        } else {
            const auto &obj = *it;
            read_string(obj, "bind_host", cfg.http.bind_host, result.errors,
                        "http", false);
            read_int(obj, "port", cfg.http.port, result.errors, "http");
            read_int64(obj, "max_payload_bytes", cfg.http.max_payload_bytes,
                       result.errors, "http");
        }
    }

    if (auto it = root.find("ws"); it != root.end()) {
        if (!it->is_object()) {
            add_error(result.errors, "ws", "type_mismatch", "Expected object");
        } else {
            const auto &obj = *it;
            read_string(obj, "bind_host", cfg.ws.bind_host, result.errors, "ws",
                        false);
            read_int(obj, "port", cfg.ws.port, result.errors, "ws");
            read_int64(obj, "max_payload_bytes", cfg.ws.max_payload_bytes,
                       result.errors, "ws");
        }
    }

    if (auto it = root.find("queues"); it != root.end()) {
        if (!it->is_object()) {
            add_error(result.errors, "queues", "type_mismatch",
                      "Expected object");
        } else {
            const auto &obj = *it;
            read_int64(obj, "high_capacity", cfg.queues.high_capacity,
                       result.errors, "queues");
            read_int64(obj, "low_capacity", cfg.queues.low_capacity,
                       result.errors, "queues");
            read_int(obj, "workers", cfg.queues.workers, result.errors,
                     "queues");
        }
    }

    if (auto it = root.find("security"); it == root.end()) {
        add_error(result.errors, "security.server_secret", "missing",
                  "Missing required field");
    } else if (!it->is_object()) {
        add_error(result.errors, "security", "type_mismatch",
                  "Expected object");
    } else {
        const auto &obj = *it;
        read_string(obj, "server_secret", cfg.security.server_secret,
                    result.errors, "security", true);

        if (auto ar = obj.find("anti_replay"); ar != obj.end()) {
            if (!ar->is_object()) {
                add_error(result.errors, "security.anti_replay",
                          "type_mismatch", "Expected object");
            } else {
                const auto &ar_obj = *ar;
                read_bool(ar_obj, "enabled", cfg.security.anti_replay.enabled,
                          result.errors, "security.anti_replay");
                read_int64(ar_obj, "max_skew_ms",
                           cfg.security.anti_replay.max_skew_ms, result.errors,
                           "security.anti_replay");
                read_int64(ar_obj, "nonce_ttl_ms",
                           cfg.security.anti_replay.nonce_ttl_ms, result.errors,
                           "security.anti_replay");
                read_int64(ar_obj, "nonce_capacity",
                           cfg.security.anti_replay.nonce_capacity,
                           result.errors, "security.anti_replay");

                if (auto scopes_it = ar_obj.find("require_for_scopes");
                    scopes_it != ar_obj.end()) {
                    if (!scopes_it->is_array()) {
                        add_error(result.errors,
                                  "security.anti_replay.require_for_scopes",
                                  "type_mismatch", "Expected array");
                    } else {
                        ScopeMask mask = 0;
                        const auto &scopes_arr = *scopes_it;
                        for (std::size_t i = 0; i < scopes_arr.size(); ++i) {
                            const auto &scope_value = scopes_arr.at(i);
                            if (!scope_value.is_string()) {
                                add_error(
                                    result.errors,
                                    "security.anti_replay.require_for_scopes[" +
                                        std::to_string(i) + "]",
                                    "type_mismatch", "Expected string");
                                continue;
                            }

                            const std::string scope_name =
                                scope_value.get<std::string>();
                            const auto parsed_scope = parse_scope(scope_name);
                            if (!parsed_scope.has_value()) {
                                std::clog
                                    << "WARN: unknown scope in "
                                       "require_for_scopes: "
                                    << scope_name << '\n';
                                continue;
                            }
                            mask |= to_scope_mask(*parsed_scope);
                        }
                        cfg.security.anti_replay.require_for_scopes = mask;
                    }
                }
            }
        }
    }

    if (auto it = root.find("auth"); it != root.end()) {
        if (!it->is_object()) {
            add_error(result.errors, "auth", "type_mismatch",
                      "Expected object");
        } else {
            auto &obj = *it;
            read_int64(obj, "cache_ttl_ms", cfg.auth.cache_ttl_ms,
                       result.errors, "auth");
            read_int64(obj, "rps_limit", cfg.auth.rps_limit, result.errors,
                       "auth");
            read_int64(obj, "ws_max_connections", cfg.auth.ws_max_connections,
                       result.errors, "auth");
            read_int64(obj, "rate_limit_window_ms",
                       cfg.auth.rate_limit_window_ms, result.errors, "auth");

            if (auto keys_it = obj.find("api_keys"); keys_it != obj.end()) {
                if (!keys_it->is_array()) {
                    add_error(result.errors, "auth.api_keys", "type_mismatch",
                              "Expected array");
                } else {
                    cfg.auth.api_keys.clear();
                    cfg.auth.api_keys.reserve(keys_it->size());

                    FingerprintComputer computer(cfg.security.server_secret);
                    for (std::size_t i = 0; i < keys_it->size(); ++i) {
                        auto &key_json = keys_it->at(i);
                        if (!key_json.is_object()) {
                            add_error(result.errors,
                                      "auth.api_keys[" + std::to_string(i) +
                                          "]",
                                      "type_mismatch", "Expected object");
                            continue;
                        }

                        if (!key_json.contains("token") ||
                            !key_json.at("token").is_string()) {
                            add_error(result.errors,
                                      "auth.api_keys[" + std::to_string(i) +
                                          "].token",
                                      key_json.contains("token")
                                          ? "type_mismatch"
                                          : "missing",
                                      key_json.contains("token")
                                          ? "Expected string"
                                          : "Missing required field");
                            continue;
                        }

                        std::string token =
                            key_json.at("token").get<std::string>();
                        std::string fingerprint;
                        try {
                            fingerprint = computer.compute(token);
                        } catch (const std::exception &) {
                            add_error(result.errors,
                                      "auth.api_keys[" + std::to_string(i) +
                                          "].token",
                                      "invalid_value",
                                      "Failed to compute fingerprint");
                        }

                        if (!token.empty()) {
                            OPENSSL_cleanse(token.data(), token.size());
                        }
                        token.clear();
                        key_json["token"] = "";

                        if (fingerprint.empty()) {
                            continue;
                        }

                        ScopeMask scope_mask = 0;
                        if (auto scopes_it = key_json.find("scopes");
                            scopes_it != key_json.end()) {
                            if (!scopes_it->is_array()) {
                                add_error(result.errors,
                                          "auth.api_keys[" + std::to_string(i) +
                                              "].scopes",
                                          "type_mismatch", "Expected array");
                            } else {
                                for (std::size_t si = 0; si < scopes_it->size();
                                     ++si) {
                                    const auto &scope_json = scopes_it->at(si);
                                    if (!scope_json.is_string()) {
                                        add_error(result.errors,
                                                  "auth.api_keys[" +
                                                      std::to_string(i) +
                                                      "].scopes[" +
                                                      std::to_string(si) + "]",
                                                  "type_mismatch",
                                                  "Expected string");
                                        continue;
                                    }

                                    auto parsed = parse_scope(
                                        scope_json.get<std::string>());
                                    if (!parsed.has_value()) {
                                        add_error(result.errors,
                                                  "auth.api_keys[" +
                                                      std::to_string(i) +
                                                      "].scopes[" +
                                                      std::to_string(si) + "]",
                                                  "invalid_value",
                                                  "Unknown scope");
                                        continue;
                                    }

                                    scope_mask = scope_mask | *parsed;
                                }
                            }
                        }

                        std::optional<std::int64_t> expires_at_ms;
                        if (auto expires_it = key_json.find("expires_at");
                            expires_it != key_json.end()) {
                            if (!expires_it->is_null()) {
                                if (!expires_it->is_number_integer()) {
                                    add_error(
                                        result.errors,
                                        "auth.api_keys[" + std::to_string(i) +
                                            "].expires_at",
                                        "type_mismatch", "Expected integer");
                                } else {
                                    expires_at_ms =
                                        expires_it->get<std::int64_t>();
                                }
                            }
                        }

                        std::int64_t rps_limit = cfg.auth.rps_limit;
                        std::int64_t ws_max_connections =
                            cfg.auth.ws_max_connections;
                        if (auto rl_it = key_json.find("rate_limit");
                            rl_it != key_json.end()) {
                            if (!rl_it->is_object()) {
                                add_error(result.errors,
                                          "auth.api_keys[" + std::to_string(i) +
                                              "].rate_limit",
                                          "type_mismatch", "Expected object");
                            } else {
                                read_int64(
                                    *rl_it, "rps", rps_limit, result.errors,
                                    "auth.api_keys[" + std::to_string(i) +
                                        "].rate_limit");
                                read_int64(*rl_it, "ws_max_connections",
                                           ws_max_connections, result.errors,
                                           "auth.api_keys[" +
                                               std::to_string(i) +
                                               "].rate_limit");
                            }
                        }

                        cfg.auth.api_keys.push_back(ApiKeyEntry{
                            fingerprint,
                            scope_mask,
                            expires_at_ms,
                            rps_limit,
                            ws_max_connections,
                        });
                    }
                }
            }
        }
    }

    if (auto it = root.find("storage"); it != root.end()) {
        if (!it->is_object()) {
            add_error(result.errors, "storage", "type_mismatch",
                      "Expected object");
        } else {
            const auto &obj = *it;
            read_string(obj, "path", cfg.storage.path, result.errors, "storage",
                        false);
            read_int64(obj, "min_free_bytes", cfg.storage.min_free_bytes,
                       result.errors, "storage");
        }
    }

    if (auto it = root.find("logging"); it != root.end()) {
        if (!it->is_object()) {
            add_error(result.errors, "logging", "type_mismatch",
                      "Expected object");
        } else {
            const auto &obj = *it;
            read_string(obj, "level", cfg.logging.level, result.errors,
                        "logging", false);
            read_bool(obj, "console", cfg.logging.console, result.errors,
                      "logging");
            read_string(obj, "file_path", cfg.logging.file_path, result.errors,
                        "logging", false);
        }
    }

    if (auto it = root.find("peers"); it != root.end()) {
        if (!it->is_array()) {
            add_error(result.errors, "peers", "type_mismatch",
                      "Expected array");
        } else {
            const auto &arr = *it;
            // Пересобираем список, чтобы исключить частично заполненные данные.
            cfg.peers.clear();
            cfg.peers.reserve(arr.size());
            for (std::size_t i = 0; i < arr.size(); ++i) {
                const auto &peer_json = arr.at(i);
                if (!peer_json.is_object()) {
                    add_error(result.errors, "peers[" + std::to_string(i) + "]",
                              "type_mismatch", "Expected object");
                    continue;
                }
                PeerConfig peer;
                if (peer_json.contains("id") &&
                    !peer_json.at("id").is_string()) {
                    add_error(result.errors,
                              "peers[" + std::to_string(i) + "].id",
                              "type_mismatch", "Expected string");
                } else if (peer_json.contains("id")) {
                    peer.id = peer_json.at("id").get<std::string>();
                }

                if (peer_json.contains("url") &&
                    !peer_json.at("url").is_string()) {
                    add_error(result.errors,
                              "peers[" + std::to_string(i) + "].url",
                              "type_mismatch", "Expected string");
                } else if (peer_json.contains("url")) {
                    peer.url = peer_json.at("url").get<std::string>();
                }

                cfg.peers.push_back(peer);
            }
        }
    }

    if (!result.errors.empty()) {
        result.config = std::nullopt;
        return result;
    }

    result.config = cfg;
    return result;
}

} // namespace dfh_node::config
