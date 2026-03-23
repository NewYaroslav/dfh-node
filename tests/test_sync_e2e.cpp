/// \file test_sync_e2e.cpp
/// \brief E2E-тесты pull-based межнодовой синхронизации.
/// \details Проверяет базовый pull, идемпотентность, divergence, ошибку
/// недоступного peer'а и freshness по `record_count`.
///
// clang-format off
#include "sync_test_support.hpp"
#include "sync.hpp"
// clang-format on

#include <filesystem>

namespace {

void cleanup_path(const std::filesystem::path &path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

void test_basic_pull_downloads_missing_block() {
    const std::string sync_token = "sync-token";
    sync_test_support::RunningSyncPeer peer(sync_token);
    const auto key = sync_test_support::make_test_key();
    peer.adapter().set_block(key, 200, 10, "peer-a");

    sync_test_support::ScriptedAdapter local_adapter;
    const auto storage_root = sync_test_support::make_temp_dir("dfh-node-sync-e2e-basic");
    std::filesystem::create_directories(storage_root);
    auto cfg = sync_test_support::make_client_config(peer.base_url(), sync_token, storage_root);
    dfh_node::PeerSyncService service(local_adapter, cfg, nullptr);
    service.sync_once();

    CHECK(local_adapter.has_block(key));
    CHECK_EQ(local_adapter.marker_for(key), "peer-a");
    CHECK_EQ(service.blocks_downloaded_total(), 1U);
    cleanup_path(storage_root);
}

void test_second_sync_is_idempotent() {
    const std::string sync_token = "sync-token";
    sync_test_support::RunningSyncPeer peer(sync_token);
    const auto key = sync_test_support::make_test_key();
    peer.adapter().set_block(key, 200, 10, "peer-a");

    sync_test_support::ScriptedAdapter local_adapter;
    const auto storage_root = sync_test_support::make_temp_dir("dfh-node-sync-e2e-idempotent");
    std::filesystem::create_directories(storage_root);
    auto cfg = sync_test_support::make_client_config(peer.base_url(), sync_token, storage_root);
    dfh_node::PeerSyncService service(local_adapter, cfg, nullptr);
    service.sync_once();
    service.sync_once();

    CHECK_EQ(service.blocks_downloaded_total(), 1U);
    CHECK_EQ(service.blocks_skipped_total(), 1U);
    CHECK_EQ(local_adapter.marker_for(key), "peer-a");
    cleanup_path(storage_root);
}

void test_divergence_replaces_local_block() {
    const std::string sync_token = "sync-token";
    sync_test_support::RunningSyncPeer peer(sync_token);
    const auto key = sync_test_support::make_test_key();
    peer.adapter().set_block(key, 200, 10, "peer-authoritative");

    sync_test_support::ScriptedAdapter local_adapter;
    local_adapter.set_block(key, 200, 10, "local-old");

    const auto storage_root = sync_test_support::make_temp_dir("dfh-node-sync-e2e-divergence");
    std::filesystem::create_directories(storage_root);
    auto cfg = sync_test_support::make_client_config(peer.base_url(), sync_token, storage_root);
    dfh_node::PeerSyncService service(local_adapter, cfg, nullptr);
    service.sync_once();

    CHECK_EQ(service.divergence_total(), 1U);
    CHECK_EQ(local_adapter.marker_for(key), "peer-authoritative");
    cleanup_path(storage_root);
}

void test_peer_unavailable_increments_error_counter() {
    sync_test_support::ScriptedAdapter local_adapter;
    const auto storage_root = sync_test_support::make_temp_dir("dfh-node-sync-e2e-unavailable");
    std::filesystem::create_directories(storage_root);

    const unsigned short unused_port = sync_test_support::acquire_free_port();
    auto cfg = sync_test_support::make_client_config("http://127.0.0.1:" + std::to_string(unused_port), "sync-token",
                                                     storage_root);
    cfg.sync.request_timeout_ms = 200;

    dfh_node::PeerSyncService service(local_adapter, cfg, nullptr);
    service.sync_once();

    CHECK(service.sync_errors_total() >= 1U);
    CHECK_EQ(service.blocks_downloaded_total(), 0U);
    cleanup_path(storage_root);
}

void test_peer_newer_by_record_count_updates_local_block() {
    const std::string sync_token = "sync-token";
    sync_test_support::RunningSyncPeer peer(sync_token);
    const auto key = sync_test_support::make_test_key();
    peer.adapter().set_block(key, 200, 100, "peer-count-100");

    sync_test_support::ScriptedAdapter local_adapter;
    local_adapter.set_block(key, 200, 50, "local-count-50");

    const auto storage_root = sync_test_support::make_temp_dir("dfh-node-sync-e2e-count");
    std::filesystem::create_directories(storage_root);
    auto cfg = sync_test_support::make_client_config(peer.base_url(), sync_token, storage_root);
    dfh_node::PeerSyncService service(local_adapter, cfg, nullptr);
    service.sync_once();

    CHECK_EQ(local_adapter.record_count_for(key), static_cast<std::size_t>(100));
    CHECK_EQ(local_adapter.marker_for(key), "peer-count-100");
    cleanup_path(storage_root);
}

} // namespace

int main() {
    test_basic_pull_downloads_missing_block();
    test_second_sync_is_idempotent();
    test_divergence_replaces_local_block();
    test_peer_unavailable_increments_error_counter();
    test_peer_newer_by_record_count_updates_local_block();
    return 0;
}
