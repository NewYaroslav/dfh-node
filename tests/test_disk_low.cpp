/// \file test_disk_low.cpp
/// \brief Интеграционные тесты disk-low gate для HTTP и WS.
/// \details Проверяет, что write-операции блокируются, а read-операции
/// продолжают работать при нехватке диска.
///
#include "adapter.hpp"
#include "auth.hpp"
#include "config.hpp"
#include "scheduler.hpp"
#include "test_helpers.hpp"
#include "transport.hpp"

#include <client_http.hpp>
#include <client_ws.hpp>
#include <nlohmann/json.hpp>

#ifdef USE_STANDALONE_ASIO
#include <asio.hpp>
#include <asio/ip/tcp.hpp>
#else
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
namespace asio = boost::asio;
#endif

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using SwsHttpClient = SimpleWeb::Client<SimpleWeb::HTTP>;
using SwsHeaders = SimpleWeb::CaseInsensitiveMultimap;
using WsClient = SimpleWeb::SocketClient<SimpleWeb::WS>;
using WsConnection = WsClient::Connection;
using WsInMessage = WsClient::InMessage;

struct HttpResponse {
    int status{0};
    std::string body;
};

struct WsFrame {
    std::uint8_t opcode{0};
    std::string payload;
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

dfh_node::config::Config make_config() {
    auto cfg = dfh_node::config::default_config();
    cfg.node_id = "node-disk-low-it";
    cfg.env = "test";
    cfg.security.server_secret = "0123456789abcdef0123456789abcdef";
    cfg.security.anti_replay.enabled = false;
    cfg.security.anti_replay.require_for_scopes = 0;
    cfg.auth.rps_limit = 100000;
    cfg.auth.rate_limit_window_ms = 1000;
    cfg.auth.cache_ttl_ms = 60000;
    cfg.http.bind_host = "127.0.0.1";
    cfg.http.port = static_cast<int>(acquire_free_port());
    cfg.http.request_timeout_ms = 1000;
    cfg.http.max_payload_bytes = 1024 * 1024;
    cfg.http.history_max_bytes = 1024 * 1024;
    cfg.ws.bind_host = "127.0.0.1";
    cfg.ws.port = static_cast<int>(acquire_free_port());
    cfg.ws.max_payload_bytes = 1024 * 1024;
    cfg.ws.request_timeout_ms = 1000;
    cfg.queues.high_capacity = 16;
    cfg.queues.low_capacity = 16;
    cfg.queues.workers = 2;
    return cfg;
}

class TestWsClient final {
public:
    TestWsClient(const std::string &endpoint, const std::string &token) : m_client(endpoint) {
        m_client.config.timeout_request = 2;
        m_client.config.timeout_idle = 2;
        m_client.config.header.emplace("Authorization", "Bearer " + token);

        m_client.on_open = [this](const std::shared_ptr<WsConnection> &connection) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_connection = connection;
            m_open = true;
            m_cv.notify_all();
        };
        m_client.on_message = [this](const std::shared_ptr<WsConnection> &,
                                     const std::shared_ptr<WsInMessage> &message) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_frames.push_back(WsFrame{message->fin_rsv_opcode, message->string()});
            m_cv.notify_all();
        };

        m_thread = std::thread([this]() { m_client.start(); });
    }

    ~TestWsClient() {
        m_client.stop();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    bool wait_open() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::seconds(2), [this]() { return m_open; });
    }

    void send_text(const std::string &payload) {
        std::shared_ptr<WsConnection> connection;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            connection = m_connection;
        }
        CHECK(connection != nullptr);
        connection->send(payload);
    }

    nlohmann::json wait_json_response() {
        std::unique_lock<std::mutex> lock(m_mutex);
        const bool ready = m_cv.wait_for(lock, std::chrono::seconds(2), [this]() { return !m_frames.empty(); });
        CHECK(ready);
        const WsFrame frame = std::move(m_frames.front());
        m_frames.pop_front();
        return nlohmann::json::parse(frame.payload);
    }

private:
    WsClient m_client;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::shared_ptr<WsConnection> m_connection;
    std::deque<WsFrame> m_frames;
    bool m_open{false};
};

class RunningDiskLowNode {
public:
    RunningDiskLowNode()
        : m_cfg(make_config()),
          m_storage_root(
              std::filesystem::temp_directory_path() /
              ("dfh-node-disk-low-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
          m_mdbx_path(m_storage_root / "keys.mdbx"), m_fingerprint_computer(m_cfg.security.server_secret),
          m_key_store(make_api_keys()), m_auth_cache(m_cfg.auth.cache_ttl_ms),
          m_auth_service(m_key_store, m_auth_cache, m_fingerprint_computer),
          m_rate_limiter(m_cfg.auth.rps_limit, m_cfg.auth.rate_limit_window_ms),
          m_gate(m_auth_service, m_rate_limiter, m_ws_connection_limiter, nullptr, 0),
          m_scheduler(static_cast<std::size_t>(m_cfg.queues.high_capacity),
                      static_cast<std::size_t>(m_cfg.queues.low_capacity)),
          m_worker_pool(static_cast<std::size_t>(m_cfg.queues.workers), m_scheduler),
          m_disk_monitor(m_storage_root.string(), std::numeric_limits<std::uint64_t>::max()),
          m_mdbx_store(m_mdbx_path.string()),
          m_http_router(m_gate, m_scheduler, m_adapter, m_cfg, &m_disk_monitor, &m_mdbx_store),
          m_http_server(m_cfg.http, m_http_router),
          m_registry(std::make_shared<dfh_node::transport::WsSessionRegistry>()),
          m_ws_router(m_gate, m_scheduler, m_adapter, m_cfg, m_registry, &m_disk_monitor),
          m_ws_server(m_cfg.ws, m_ws_router) {
        std::filesystem::create_directories(m_storage_root);
        m_mdbx_store.open();
        m_worker_pool.start();
        m_http_server.start();
        m_ws_server.start();
        wait_until_ready();
    }

    ~RunningDiskLowNode() {
        m_http_server.shutdown();
        m_ws_server.shutdown();
        m_worker_pool.shutdown();
        m_scheduler.shutdown();
    }

    HttpResponse http_request(const std::string &method, const std::string &path, const std::string &body = "") const {
        SwsHttpClient client("127.0.0.1:" + std::to_string(m_cfg.http.port));
        client.config.timeout = 5;
        SwsHeaders headers;
        headers.emplace("Authorization", "Bearer " + m_token);
        if (!body.empty()) {
            headers.emplace("Content-Type", "application/json");
        }

        auto response = client.request(method, path, body, headers);
        return HttpResponse{parse_status_code(response->status_code), response->content.string()};
    }

    std::string ws_endpoint(const std::string &path) const {
        return "127.0.0.1:" + std::to_string(m_cfg.ws.port) + path;
    }

    const std::string &token() const { return m_token; }

private:
    std::vector<dfh_node::config::ApiKeyEntry> make_api_keys() const {
        return {
            {m_fingerprint_computer.compute(m_token),
             dfh_node::to_scope_mask(dfh_node::Scope::Read) | dfh_node::to_scope_mask(dfh_node::Scope::Write),
             std::nullopt, 100000, 10},
        };
    }

    void wait_until_ready() const {
        for (int attempt = 0; attempt < 100; ++attempt) {
            try {
                const auto response =
                    http_request("GET", "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks"
                                        "&from_ms=1&to_ms=2&format=csv");
                if (response.status == 200) {
                    return;
                }
            } catch (...) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CHECK(false);
    }

    dfh_node::config::Config m_cfg;
    std::filesystem::path m_storage_root;
    std::filesystem::path m_mdbx_path;
    std::string m_token{"rw-token"};
    dfh_node::FingerprintComputer m_fingerprint_computer;
    dfh_node::ConfigApiKeyStore m_key_store;
    dfh_node::AuthCache m_auth_cache;
    dfh_node::AuthService m_auth_service;
    dfh_node::RateLimiter m_rate_limiter;
    dfh_node::WsConnectionLimiter m_ws_connection_limiter;
    dfh_node::UnifiedGate m_gate;
    dfh_node::TaskScheduler m_scheduler;
    dfh_node::WorkerPool m_worker_pool;
    dfh_node::DiskMonitor m_disk_monitor;
    dfh_node::MdbxApiKeyStore m_mdbx_store;
    dfh_node::FakeDfhAdapter m_adapter;
    dfh_node::transport::HttpRouter m_http_router;
    dfh_node::transport::HttpServer m_http_server;
    std::shared_ptr<dfh_node::transport::WsSessionRegistry> m_registry;
    dfh_node::transport::WsRouter m_ws_router;
    dfh_node::transport::WsServer m_ws_server;
};

nlohmann::json make_ingest_control(const std::string &msg_id) {
    return {
        {"op", "ingest"},
        {"msg_id", msg_id},
        {"payload",
         {{"provider", "binance"},
          {"symbol", "BTCUSDT"},
          {"source", "spot"},
          {"tf", "ticks"},
          {"block_ts", 1704067200000LL},
          {"payload_base64", "AQID"}}},
    };
}

nlohmann::json make_history_control(const std::string &msg_id) {
    return {
        {"op", "history"},
        {"msg_id", msg_id},
        {"payload",
         {{"provider", "binance"},
          {"symbol", "BTCUSDT"},
          {"source", "spot"},
          {"tf", "ticks"},
          {"from_ms", 1},
          {"to_ms", 2}}},
    };
}

void test_http_disk_low_blocks_only_ingest() {
    RunningDiskLowNode node;

    const std::string ingest_body =
        R"([{"key":{"provider":"binance","symbol":"BTCUSDT","source":"spot","tf":"ticks","block_ts":1704067200000},"payload_base64":"AQID"}])";
    const auto ingest = node.http_request("POST", "/v1/ingest", ingest_body);
    CHECK_EQ(ingest.status, 507);
    CHECK_EQ(nlohmann::json::parse(ingest.body).at("error").get<std::string>(), "disk_low");

    const auto history = node.http_request(
        "GET", "/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1&to_ms=2&format=csv");
    CHECK_EQ(history.status, 200);
}

void test_ws_disk_low_blocks_only_ingest() {
    RunningDiskLowNode node;
    TestWsClient client(node.ws_endpoint("/ws/json"), node.token());
    CHECK(client.wait_open());

    client.send_text(make_ingest_control("ing-1").dump());
    const auto ingest = client.wait_json_response();
    CHECK_EQ(ingest.at("ok").get<bool>(), false);
    CHECK_EQ(ingest.at("error_code").get<std::string>(), "disk_low");

    client.send_text(make_history_control("hist-1").dump());
    const auto history = client.wait_json_response();
    CHECK_EQ(history.at("ok").get<bool>(), true);
}

} // namespace

int main() {
    test_http_disk_low_blocks_only_ingest();
    test_ws_disk_low_blocks_only_ingest();
    return 0;
}
