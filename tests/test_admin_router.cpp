/// \file test_admin_router.cpp
/// \brief Интеграционные тесты для AdminRouter.
/// \details Поднимает реальный HTTP-сервер и проверяет базовые сценарии
/// авторизации, anti-replay и CRUD для динамических API-ключей.
///
#include "adapter.hpp"
#include "auth.hpp"
#include "config.hpp"
#include "scheduler.hpp"
#include "security.hpp"
#include "test_helpers.hpp"
#include "transport.hpp"

#include <client_http.hpp>
#include <nlohmann/json.hpp>

#ifdef USE_STANDALONE_ASIO
#include <asio.hpp>
#include <asio/ip/tcp.hpp>
#else
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
namespace asio = boost::asio;
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using SwsClient = SimpleWeb::Client<SimpleWeb::HTTP>;
using SwsHeaders = SimpleWeb::CaseInsensitiveMultimap;

struct HttpResponse {
    int status{0};
    std::string body;
    SwsHeaders headers;
};

class EpochSystemClock final : public dfh_node::IClock {
public:
    std::uint64_t now_ms() const override {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
    }
};

int parse_status_code(const std::string &value) {
    CHECK(value.size() >= 3);
    return std::stoi(value.substr(0, 3));
}

unsigned short acquire_free_port() {
    asio::io_service io_service;
    asio::ip::tcp::acceptor acceptor(io_service);
    acceptor.open(asio::ip::tcp::v4());
    acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    const unsigned short port = acceptor.local_endpoint().port();
    acceptor.close();
    return port;
}

std::int64_t now_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string next_nonce() {
    static std::atomic<std::uint64_t> counter{1};
    constexpr char digits[] = "0123456789abcdef";
    std::uint64_t value = counter.fetch_add(1, std::memory_order_relaxed);
    std::string nonce(16, '0');
    for (int index = 15; index >= 0; --index) {
        nonce[static_cast<std::size_t>(index)] = digits[value & 0x0fU];
        value >>= 4U;
    }
    return nonce;
}

void split_path_and_query(const std::string &url, std::string &path,
                          std::vector<std::pair<std::string, std::string>> &query) {
    path = url;
    query.clear();

    const auto question = url.find('?');
    if (question == std::string::npos) {
        return;
    }

    path = url.substr(0, question);
    const std::string query_string = url.substr(question + 1);
    std::size_t start = 0;
    while (start <= query_string.size()) {
        const std::size_t amp = query_string.find('&', start);
        const std::string token =
            (amp == std::string::npos) ? query_string.substr(start) : query_string.substr(start, amp - start);
        if (!token.empty()) {
            const std::size_t eq = token.find('=');
            const std::string key = token.substr(0, eq);
            const std::string value = (eq == std::string::npos) ? std::string() : token.substr(eq + 1);
            query.emplace_back(key, value);
        }
        if (amp == std::string::npos) {
            break;
        }
        start = amp + 1;
    }
}

SwsHeaders make_anti_replay_headers(const std::string &method, const std::string &url, const std::string &body,
                                    const std::string &token) {
    std::string path;
    std::vector<std::pair<std::string, std::string>> query;
    split_path_and_query(url, path, query);

    const std::string timestamp = std::to_string(now_epoch_ms());
    const std::string nonce = next_nonce();
    const std::string body_hash = dfh_node::compute_sha256_hex(body);

    unsigned char signing_key[32];
    dfh_node::compute_sha256_raw(token, signing_key);

    dfh_node::HttpCanonicalInput input;
    input.method = method;
    input.path = path;
    input.query_params = std::move(query);
    input.timestamp = timestamp;
    input.nonce = nonce;
    input.body_hash = body_hash;

    SwsHeaders headers;
    headers.emplace("X-DFH-Timestamp", timestamp);
    headers.emplace("X-DFH-Nonce", nonce);
    headers.emplace("X-DFH-Signature",
                    dfh_node::compute_signature(dfh_node::canonicalize_http(input), signing_key, 32));
    return headers;
}

dfh_node::config::Config make_config() {
    auto cfg = dfh_node::config::default_config();
    cfg.node_id = "node-admin-it";
    cfg.env = "test";
    cfg.security.server_secret = "0123456789abcdef0123456789abcdef";
    cfg.security.anti_replay.enabled = true;
    cfg.security.anti_replay.max_skew_ms = 5000;
    cfg.security.anti_replay.nonce_ttl_ms = 60000;
    cfg.security.anti_replay.nonce_capacity = 1000;
    cfg.security.anti_replay.require_for_scopes =
        dfh_node::to_scope_mask(dfh_node::Scope::Write) | dfh_node::to_scope_mask(dfh_node::Scope::Sync);
    cfg.auth.rps_limit = 100000;
    cfg.auth.rate_limit_window_ms = 1000;
    cfg.auth.cache_ttl_ms = 60000;
    cfg.http.bind_host = "127.0.0.1";
    cfg.http.port = static_cast<int>(acquire_free_port());
    cfg.http.request_timeout_ms = 1000;
    cfg.http.max_payload_bytes = 1024 * 1024;
    cfg.queues.high_capacity = 16;
    cfg.queues.low_capacity = 16;
    cfg.queues.workers = 1;
    return cfg;
}

class RunningAdminNode {
public:
    RunningAdminNode()
        : m_cfg(make_config()),
          m_storage_root(
              std::filesystem::temp_directory_path() /
              ("dfh-node-admin-router-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
          m_mdbx_path(m_storage_root / "keys.mdbx"), m_fingerprint_computer(m_cfg.security.server_secret),
          m_config_store(make_api_keys()), m_auth_cache(m_cfg.auth.cache_ttl_ms), m_mdbx_store(m_mdbx_path.string()),
          m_composite_store(m_config_store, m_mdbx_store),
          m_auth_service(m_composite_store, m_auth_cache, m_fingerprint_computer),
          m_rate_limiter(m_cfg.auth.rps_limit, m_cfg.auth.rate_limit_window_ms),
          m_nonce_store(m_epoch_clock, m_cfg.security.anti_replay.nonce_ttl_ms,
                        m_cfg.security.anti_replay.nonce_capacity),
          m_anti_replay_validator(m_cfg.security.anti_replay, m_epoch_clock, m_nonce_store),
          m_gate(m_auth_service, m_rate_limiter, m_ws_connection_limiter, &m_anti_replay_validator,
                 m_cfg.security.anti_replay.require_for_scopes),
          m_scheduler(static_cast<std::size_t>(m_cfg.queues.high_capacity),
                      static_cast<std::size_t>(m_cfg.queues.low_capacity)),
          m_worker_pool(static_cast<std::size_t>(m_cfg.queues.workers), m_scheduler),
          m_http_router(m_gate, m_scheduler, m_adapter, m_cfg, nullptr, &m_mdbx_store),
          m_http_server(m_cfg.http, m_http_router),
          m_key_manager(m_mdbx_store, m_auth_cache, m_fingerprint_computer, m_cfg.auth),
          m_admin_router(m_gate, m_key_manager, m_cfg) {
        std::filesystem::create_directories(m_storage_root);
        m_mdbx_store.open();
        m_worker_pool.start();
        m_admin_router.register_all(m_http_server.server());
        m_http_server.start();
        wait_until_ready();
    }

    ~RunningAdminNode() {
        m_http_server.shutdown();
        m_worker_pool.shutdown();
        m_scheduler.shutdown();
    }

    HttpResponse request(const std::string &method, const std::string &url, const std::string &body = "",
                         const std::optional<std::string> &token = std::nullopt,
                         const SwsHeaders &extra_headers = SwsHeaders()) const {
        SwsClient client("127.0.0.1:" + std::to_string(m_cfg.http.port));
        client.config.timeout = 5;

        SwsHeaders headers = extra_headers;
        if (token.has_value()) {
            headers.emplace("Authorization", "Bearer " + *token);
        }
        if (!body.empty()) {
            headers.emplace("Content-Type", "application/json");
        }

        auto response = client.request(method, url, body, headers);
        HttpResponse result;
        result.status = parse_status_code(response->status_code);
        result.body = response->content.string();
        result.headers = response->header;
        return result;
    }

    SwsHeaders admin_ar_headers(const std::string &method, const std::string &url, const std::string &body = "") const {
        return make_anti_replay_headers(method, url, body, m_admin_token);
    }

    SwsHeaders read_ar_headers(const std::string &method, const std::string &url, const std::string &body = "") const {
        return make_anti_replay_headers(method, url, body, m_read_token);
    }

    const std::string &admin_token() const { return m_admin_token; }
    const std::string &read_token() const { return m_read_token; }

private:
    std::vector<dfh_node::config::ApiKeyEntry> make_api_keys() const {
        return {
            {m_fingerprint_computer.compute(m_admin_token), dfh_node::to_scope_mask(dfh_node::Scope::Admin),
             std::nullopt, 100000, 10},
            {m_fingerprint_computer.compute(m_read_token), dfh_node::to_scope_mask(dfh_node::Scope::Read), std::nullopt,
             100000, 10},
        };
    }

    void wait_until_ready() const {
        for (int attempt = 0; attempt < 100; ++attempt) {
            asio::io_service io_service;
            asio::ip::tcp::socket socket(io_service);
            asio::error_code ec;
            socket.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"),
                                                   static_cast<unsigned short>(m_cfg.http.port)),
                           ec);
            if (!ec) {
                socket.close();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CHECK(false);
    }

    dfh_node::config::Config m_cfg;
    std::filesystem::path m_storage_root;
    std::filesystem::path m_mdbx_path;
    std::string m_admin_token{"admin-token"};
    std::string m_read_token{"read-token"};
    EpochSystemClock m_epoch_clock;
    dfh_node::FingerprintComputer m_fingerprint_computer;
    dfh_node::ConfigApiKeyStore m_config_store;
    dfh_node::AuthCache m_auth_cache;
    dfh_node::MdbxApiKeyStore m_mdbx_store;
    dfh_node::CompositeApiKeyStore m_composite_store;
    dfh_node::AuthService m_auth_service;
    dfh_node::RateLimiter m_rate_limiter;
    dfh_node::WsConnectionLimiter m_ws_connection_limiter;
    dfh_node::NonceStore m_nonce_store;
    dfh_node::AntiReplayValidator m_anti_replay_validator;
    dfh_node::UnifiedGate m_gate;
    dfh_node::TaskScheduler m_scheduler;
    dfh_node::WorkerPool m_worker_pool;
    dfh_node::FakeDfhAdapter m_adapter;
    dfh_node::transport::HttpRouter m_http_router;
    dfh_node::transport::HttpServer m_http_server;
    dfh_node::ApiKeyManager m_key_manager;
    dfh_node::transport::AdminRouter m_admin_router;
};

void test_unauthorized_forbidden_and_missing_anti_replay() {
    RunningAdminNode node;
    const std::string body = R"({"name":"k1","scopes":["read"]})";

    const auto unauthorized = node.request("POST", "/v1/admin/keys", body);
    CHECK_EQ(unauthorized.status, 400);
    CHECK_EQ(nlohmann::json::parse(unauthorized.body).at("error").get<std::string>(), "missing_anti_replay_headers");

    const auto missing_ar = node.request("POST", "/v1/admin/keys", body, node.admin_token());
    CHECK_EQ(missing_ar.status, 400);
    CHECK_EQ(nlohmann::json::parse(missing_ar.body).at("error").get<std::string>(), "missing_anti_replay_headers");

    const auto forbidden = node.request("POST", "/v1/admin/keys", body, node.read_token(),
                                        node.read_ar_headers("POST", "/v1/admin/keys", body));
    CHECK_EQ(forbidden.status, 403);
    CHECK_EQ(nlohmann::json::parse(forbidden.body).at("error").get<std::string>(), "forbidden");
}

void test_create_get_update_list_revoke_delete_cycle() {
    RunningAdminNode node;
    const std::string create_body =
        R"({"name":"writer","scopes":["read","write"],"rps_limit":15,"ws_max_connections":2})";
    const auto created = node.request("POST", "/v1/admin/keys", create_body, node.admin_token(),
                                      node.admin_ar_headers("POST", "/v1/admin/keys", create_body));
    CHECK_EQ(created.status, 201);

    const auto created_json = nlohmann::json::parse(created.body);
    CHECK(created_json.contains("id"));
    CHECK(created_json.contains("token"));
    CHECK_EQ(created_json.at("name").get<std::string>(), "writer");

    const std::string id = created_json.at("id").get<std::string>();
    const auto fetched = node.request("GET", "/v1/admin/keys/" + id, "", node.admin_token());
    CHECK_EQ(fetched.status, 200);
    CHECK_EQ(nlohmann::json::parse(fetched.body).at("id").get<std::string>(), id);

    const std::string update_body = R"({"name":"writer-2","rps_limit":100})";
    const auto updated = node.request("PUT", "/v1/admin/keys/" + id, update_body, node.admin_token(),
                                      node.admin_ar_headers("PUT", "/v1/admin/keys/" + id, update_body));
    CHECK_EQ(updated.status, 200);
    const auto updated_json = nlohmann::json::parse(updated.body);
    CHECK_EQ(updated_json.at("name").get<std::string>(), "writer-2");
    CHECK_EQ(updated_json.at("rps_limit").get<std::int64_t>(), 100);

    const auto list_active = node.request("GET", "/v1/admin/keys", "", node.admin_token());
    CHECK_EQ(list_active.status, 200);
    CHECK_EQ(nlohmann::json::parse(list_active.body).size(), static_cast<std::size_t>(1));

    const auto revoked = node.request("POST", "/v1/admin/keys/" + id + "/revoke", "", node.admin_token(),
                                      node.admin_ar_headers("POST", "/v1/admin/keys/" + id + "/revoke"));
    CHECK_EQ(revoked.status, 200);
    CHECK_EQ(nlohmann::json::parse(revoked.body).at("already_revoked").get<bool>(), false);

    const auto list_all = node.request("GET", "/v1/admin/keys?status=all", "", node.admin_token());
    CHECK_EQ(list_all.status, 200);
    const auto all_json = nlohmann::json::parse(list_all.body);
    CHECK_EQ(all_json.size(), static_cast<std::size_t>(1));
    CHECK_EQ(all_json.at(0).at("revoked").get<bool>(), true);

    const auto deleted = node.request("DELETE", "/v1/admin/keys/" + id, "", node.admin_token(),
                                      node.admin_ar_headers("DELETE", "/v1/admin/keys/" + id));
    CHECK_EQ(deleted.status, 204);

    const auto missing = node.request("GET", "/v1/admin/keys/" + id, "", node.admin_token());
    CHECK_EQ(missing.status, 404);
}

void test_duplicate_name_returns_409() {
    RunningAdminNode node;
    const std::string body = R"({"name":"dup","scopes":["read"]})";

    const auto first = node.request("POST", "/v1/admin/keys", body, node.admin_token(),
                                    node.admin_ar_headers("POST", "/v1/admin/keys", body));
    CHECK_EQ(first.status, 201);

    const auto second = node.request("POST", "/v1/admin/keys", body, node.admin_token(),
                                     node.admin_ar_headers("POST", "/v1/admin/keys", body));
    CHECK_EQ(second.status, 409);
    CHECK_EQ(nlohmann::json::parse(second.body).at("error").get<std::string>(), "duplicate_name");
}

void test_revoke_is_idempotent() {
    RunningAdminNode node;
    const std::string body = R"({"name":"revoke-me","scopes":["write"]})";
    const auto created = node.request("POST", "/v1/admin/keys", body, node.admin_token(),
                                      node.admin_ar_headers("POST", "/v1/admin/keys", body));
    CHECK_EQ(created.status, 201);
    const std::string id = nlohmann::json::parse(created.body).at("id").get<std::string>();

    const auto first = node.request("POST", "/v1/admin/keys/" + id + "/revoke", "", node.admin_token(),
                                    node.admin_ar_headers("POST", "/v1/admin/keys/" + id + "/revoke"));
    CHECK_EQ(first.status, 200);
    CHECK_EQ(nlohmann::json::parse(first.body).at("already_revoked").get<bool>(), false);

    const auto second = node.request("POST", "/v1/admin/keys/" + id + "/revoke", "", node.admin_token(),
                                     node.admin_ar_headers("POST", "/v1/admin/keys/" + id + "/revoke"));
    CHECK_EQ(second.status, 200);
    CHECK_EQ(nlohmann::json::parse(second.body).at("already_revoked").get<bool>(), true);
}

} // namespace

int main() {
    test_unauthorized_forbidden_and_missing_anti_replay();
    test_create_get_update_list_revoke_delete_cycle();
    test_duplicate_name_returns_409();
    test_revoke_is_idempotent();
    return 0;
}
