#include "dfh_node/config_loader.hpp"

#include "dfh_node/config.hpp"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace dfh_node::config {
namespace {

using nlohmann::json;

std::string join_path(const std::string &prefix, const std::string &field) {
    if (prefix.empty()) {
        return field;
    }
    return prefix + "." + field;
}

void add_error(std::vector<LoadError> &errors, const std::string &path,
               const std::string &code, const std::string &message) {
    errors.push_back(LoadError{path, code, message});
}

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
        root = json::parse(buffer.str());
    } catch (const json::parse_error &ex) {
        add_error(result.errors, path.string(), "parse_error", ex.what());
        return result;
    }

    if (!root.is_object()) {
        add_error(result.errors, "", "type_mismatch", "Expected JSON object");
        return result;
    }

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
            read_int64(obj, "ingest_capacity", cfg.queues.ingest_capacity,
                       result.errors, "queues");
            read_int64(obj, "history_capacity", cfg.queues.history_capacity,
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
