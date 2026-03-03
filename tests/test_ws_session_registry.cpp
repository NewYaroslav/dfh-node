/// \file test_ws_session_registry.cpp
/// \brief Unit-тесты реестра WS-сессий.
/// \details Проверяет регистрацию/удаление соединений, обратный lookup и
/// хранение pending-состояния `dfhbin`.
///
#include "test_helpers.hpp"
#include "transport.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

using Registry = dfh_node::transport::WsSessionRegistry;

SimpleWeb::io_context &test_io_context() {
    // Контекст намеренно живёт до завершения процесса, чтобы сокеты тестовых
    // соединений не держали висячую ссылку.
    static auto *io = new SimpleWeb::io_context();
    return *io;
}

std::shared_ptr<Registry::SwsConnection> make_connection() {
    auto socket = std::make_unique<SimpleWeb::WS>(test_io_context());
    return std::make_shared<Registry::SwsConnection>(std::move(socket));
}

dfh_node::transport::WsConnectionContext make_context(const std::string &fingerprint, const std::string &endpoint) {
    dfh_node::transport::WsConnectionContext ctx;
    ctx.fingerprint = fingerprint;
    ctx.scope_mask = dfh_node::to_scope_mask(dfh_node::Scope::Write);
    ctx.endpoint = endpoint;
    ctx.connected_at = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < std::size(ctx.signing_key); ++i) {
        ctx.signing_key[i] = static_cast<std::uint8_t>(i);
    }
    return ctx;
}

void test_register_and_unregister() {
    Registry registry;
    auto conn = make_connection();
    const auto id = registry.register_connection(conn, make_context("fp-1", "/ws/json"));
    CHECK_EQ(registry.size(), static_cast<std::size_t>(1));
    CHECK(!id.empty());

    registry.unregister_connection(id);
    CHECK_EQ(registry.size(), static_cast<std::size_t>(0));
}

void test_get_connection_after_unregister() {
    Registry registry;
    auto conn = make_connection();
    const auto id = registry.register_connection(conn, make_context("fp-2", "/ws/json"));

    registry.unregister_connection(id);
    const auto weak = registry.get_connection(id);
    CHECK(weak.lock() == nullptr);
}

void test_find_id_and_context() {
    Registry registry;
    auto conn = make_connection();
    auto ctx = make_context("fp-3", "/ws/msgpack");
    const auto id = registry.register_connection(conn, std::move(ctx));

    const auto found = registry.find_id(conn.get());
    CHECK(found.has_value());
    CHECK_EQ(*found, id);

    const auto loaded_ctx = registry.get_context(id);
    CHECK(loaded_ctx.has_value());
    CHECK_EQ(loaded_ctx->connection_id, id);
    CHECK_EQ(loaded_ctx->fingerprint, "fp-3");
    CHECK_EQ(loaded_ctx->endpoint, "/ws/msgpack");

    registry.unregister_connection(id);
    CHECK(!registry.find_id(conn.get()).has_value());
}

void test_pending_dfhbin_state() {
    Registry registry;
    auto conn = make_connection();
    const auto id = registry.register_connection(conn, make_context("fp-4", "/ws/json"));

    dfh_node::transport::PendingDfhbinState state;
    state.msg_id = "msg-1";
    state.payload_sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    state.block_key.provider = "binance";
    state.block_key.symbol = "BTCUSDT";
    state.block_key.source = "spot";
    state.block_key.tf = dfh_node::Timeframe::Ticks;
    state.block_key.block_ts = 1704067200000;

    registry.set_pending_dfhbin(id, std::move(state));
    auto taken = registry.take_pending_dfhbin(id);
    CHECK(taken.has_value());
    CHECK_EQ(taken->msg_id, "msg-1");
    CHECK_EQ(taken->block_key.symbol, "BTCUSDT");
    CHECK(!registry.take_pending_dfhbin(id).has_value());

    dfh_node::transport::PendingDfhbinState state2;
    state2.msg_id = "msg-2";
    state2.payload_sha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    state2.block_key.provider = "kraken";
    state2.block_key.symbol = "ETHUSD";
    state2.block_key.source = "spot";
    state2.block_key.tf = dfh_node::Timeframe::M1;
    state2.block_key.block_ts = 1704067260000;
    registry.set_pending_dfhbin(id, std::move(state2));
    registry.clear_pending_dfhbin(id);
    CHECK(!registry.take_pending_dfhbin(id).has_value());
}

void test_unique_ids() {
    Registry registry;
    std::set<std::string> ids;
    std::vector<std::shared_ptr<Registry::SwsConnection>> connections;
    for (int i = 0; i < 10; ++i) {
        auto conn = make_connection();
        auto id = registry.register_connection(conn, make_context("fp-u-" + std::to_string(i), "/ws/json"));
        ids.insert(id);
        connections.push_back(std::move(conn));
    }
    CHECK_EQ(ids.size(), static_cast<std::size_t>(10));
}

void test_concurrent_register_unregister() {
    Registry registry;

    auto worker = [&registry](int thread_index) {
        for (int i = 0; i < 50; ++i) {
            auto conn = make_connection();
            const auto id = registry.register_connection(
                conn, make_context("fp-c-" + std::to_string(thread_index) + "-" + std::to_string(i), "/ws/json"));
            registry.unregister_connection(id);
        }
    };

    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();
    t2.join();

    CHECK_EQ(registry.size(), static_cast<std::size_t>(0));
}

} // namespace

int main() {
    test_register_and_unregister();
    test_get_connection_after_unregister();
    test_find_id_and_context();
    test_pending_dfhbin_state();
    test_unique_ids();
    test_concurrent_register_unregister();
    return 0;
}
