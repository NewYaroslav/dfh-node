/// \file test_ws_runtime_components.cpp
/// \brief Unit-тесты защитных веток WS runtime-компонентов.
/// \details Проверяет проверки `null`-registry в конструкторах и
/// идемпотентность `WsServer::start()/shutdown()`.
///
#include "adapter.hpp"
#include "auth.hpp"
#include "config.hpp"
#include "scheduler.hpp"
#include "test_helpers.hpp"
#include "transport/ws/ws_message_handler.hpp"
#include "transport/ws/ws_router.hpp"
#include "transport/ws/ws_server.hpp"

#ifdef USE_STANDALONE_ASIO
#include <asio.hpp>
#include <asio/ip/tcp.hpp>
#else
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
namespace asio = boost::asio;
#endif

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

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
    cfg.security.server_secret = "0123456789abcdef0123456789abcdef";
    cfg.security.anti_replay.enabled = false;
    cfg.security.anti_replay.require_for_scopes = 0;
    cfg.auth.rps_limit = 1000;
    cfg.auth.rate_limit_window_ms = 1000;
    cfg.auth.cache_ttl_ms = 60000;
    cfg.queues.high_capacity = 8;
    cfg.queues.low_capacity = 8;
    cfg.queues.workers = 0;
    cfg.ws.bind_host = "127.0.0.1";
    cfg.ws.port = static_cast<int>(acquire_free_port());
    cfg.ws.max_payload_bytes = 1024 * 1024;
    cfg.ws.request_timeout_ms = 1000;
    return cfg;
}

class DummyAdapter final : public dfh_node::IDfhAdapter {
public:
    std::unique_ptr<dfh_node::IngestResponse> ingest_structured(std::unique_ptr<dfh_node::IngestRequest>) override {
        auto resp = std::make_unique<dfh_node::IngestResponse>();
        resp->status = dfh_node::AdapterStatus::Ok;
        return resp;
    }

    std::unique_ptr<dfh_node::QueryHistoryResponse>
    query_history(std::unique_ptr<dfh_node::QueryHistoryRequest>) override {
        auto resp = std::make_unique<dfh_node::QueryHistoryResponse>();
        resp->status = dfh_node::AdapterStatus::Ok;
        return resp;
    }

    std::unique_ptr<dfh_node::GetBlockDfhbinResponse>
    get_block_dfhbin(std::unique_ptr<dfh_node::GetBlockDfhbinRequest>) override {
        auto resp = std::make_unique<dfh_node::GetBlockDfhbinResponse>();
        resp->status = dfh_node::AdapterStatus::Error;
        resp->error_code = "not_supported";
        return resp;
    }

    std::unique_ptr<dfh_node::ListBlockMetaResponse>
    list_block_meta(std::unique_ptr<dfh_node::ListBlockMetaRequest>) override {
        auto resp = std::make_unique<dfh_node::ListBlockMetaResponse>();
        resp->status = dfh_node::AdapterStatus::Error;
        resp->error_code = "not_supported";
        return resp;
    }

    std::unique_ptr<dfh_node::GetBlockHashResponse>
    get_block_hash(std::unique_ptr<dfh_node::GetBlockHashRequest>) override {
        auto resp = std::make_unique<dfh_node::GetBlockHashResponse>();
        resp->status = dfh_node::AdapterStatus::Error;
        resp->error_code = "not_supported";
        return resp;
    }
};

struct WsFixture {
    explicit WsFixture(const dfh_node::config::Config &cfg)
        : computer(cfg.security.server_secret), store(make_entries(computer)), auth_service(store, cache, computer),
          gate(auth_service, limiter, ws_limiter, nullptr, 0),
          scheduler(static_cast<std::size_t>(cfg.queues.high_capacity),
                    static_cast<std::size_t>(cfg.queues.low_capacity)),
          adapter() {}

    static std::vector<dfh_node::config::ApiKeyEntry> make_entries(const dfh_node::FingerprintComputer &computer) {
        return {dfh_node::config::ApiKeyEntry{
            computer.compute("token"),
            dfh_node::to_scope_mask(dfh_node::Scope::Read) | dfh_node::to_scope_mask(dfh_node::Scope::Write),
            std::nullopt,
            1000,
            10,
        }};
    }

    dfh_node::FingerprintComputer computer;
    dfh_node::ConfigApiKeyStore store;
    dfh_node::AuthCache cache{60000};
    dfh_node::AuthService auth_service;
    dfh_node::RateLimiter limiter{1000, 1000};
    dfh_node::WsConnectionLimiter ws_limiter;
    dfh_node::UnifiedGate gate;
    dfh_node::TaskScheduler scheduler;
    DummyAdapter adapter;
};

void test_ws_router_ctor_rejects_null_registry() {
    const auto cfg = make_config();
    WsFixture fixture(cfg);
    bool thrown = false;
    try {
        dfh_node::transport::WsRouter router(fixture.gate, fixture.scheduler, fixture.adapter, cfg, nullptr);
        (void)router;
    } catch (const std::invalid_argument &) {
        thrown = true;
    }
    CHECK(thrown);
}

void test_ws_message_handler_ctor_rejects_null_registry() {
    const auto cfg = make_config();
    WsFixture fixture(cfg);
    bool thrown = false;
    try {
        dfh_node::transport::WsMessageHandler handler(fixture.gate, fixture.scheduler, fixture.adapter, cfg, nullptr);
        (void)handler;
    } catch (const std::invalid_argument &) {
        thrown = true;
    }
    CHECK(thrown);
}

void test_ws_server_shutdown_before_start_is_noop() {
    const auto cfg = make_config();
    WsFixture fixture(cfg);
    auto registry = std::make_shared<dfh_node::transport::WsSessionRegistry>();
    dfh_node::transport::WsRouter router(fixture.gate, fixture.scheduler, fixture.adapter, cfg, registry);
    dfh_node::transport::WsServer server(cfg.ws, router);

    server.shutdown();
}

} // namespace

int main() {
    test_ws_router_ctor_rejects_null_registry();
    test_ws_message_handler_ctor_rejects_null_registry();
    test_ws_server_shutdown_before_start_is_noop();
    return 0;
}
