#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dfh_node::config {

struct HttpConfig {
    std::string bind_host = "0.0.0.0";
    int port = 8080;
    std::int64_t max_payload_bytes = 10000000;
};

struct WsConfig {
    std::string bind_host = "0.0.0.0";
    int port = 8081;
    std::int64_t max_payload_bytes = 10000000;
};

struct QueuesConfig {
    std::int64_t ingest_capacity = 10000;
    std::int64_t history_capacity = 5000;
    int workers = 4;
};

struct AntiReplayConfig {
    bool enabled = true;
    std::int64_t max_skew_ms = 5000;
    std::int64_t nonce_ttl_ms = 60000;
    std::int64_t nonce_capacity = 10000;
};

struct SecurityConfig {
    std::string server_secret;
    AntiReplayConfig anti_replay{};
};

struct StorageConfig {
    std::string path = "./data";
    std::int64_t min_free_bytes = 2000000000;
};

struct LoggingConfig {
    std::string level = "info";
    bool console = true;
    std::string file_path;
};

struct PeerConfig {
    std::string id;
    std::string url;
};

struct Config {
    int schema_version = 1;
    std::string node_id;
    std::string env;
    HttpConfig http{};
    WsConfig ws{};
    QueuesConfig queues{};
    SecurityConfig security{};
    std::vector<PeerConfig> peers{};
    StorageConfig storage{};
    LoggingConfig logging{};
};

Config default_config();

} // namespace dfh_node::config
