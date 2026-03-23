/// \file test_peer_sync_service.cpp
/// \brief Тесты `PeerSyncService`.
/// \details Проверяет freshness/divergence-правила, счётчики и lifecycle
/// pull-loop на реальном HTTP sync-endpoint'е peer-ноды.
///
// clang-format off
#include "sync_test_support.hpp"
#include "sync.hpp"
// clang-format on

#include <filesystem>
#include <limits>

namespace {

void cleanup_path(const std::filesystem::path &path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

void test_freshness_by_last_ts_downloads_block() {
    const std::string sync_token = "sync-token";
    sync_test_support::RunningSyncPeer peer(sync_token);
    const auto key = sync_test_support::make_test_key();
    peer.adapter().set_block(key, 200, 1, "remote-newer");

    sync_test_support::ScriptedAdapter local_adapter;
    local_adapter.set_block(key, 100, 1, "local-older");

    const auto storage_root = sync_test_support::make_temp_dir("dfh-node-peer-sync-last-ts");
    std::filesystem::create_directories(storage_root);
    auto cfg = sync_test_support::make_client_config(peer.base_url(), sync_token, storage_root);
    dfh_node::PeerSyncService service(local_adapter, cfg, nullptr);
    service.sync_once();

    CHECK_EQ(service.blocks_downloaded_total(), 1U);
    CHECK_EQ(service.blocks_merged_total(), 1U);
    CHECK_EQ(local_adapter.marker_for(key), "remote-newer");
    CHECK_EQ(local_adapter.last_ts_for(key), static_cast<std::int64_t>(200));
    cleanup_path(storage_root);
}

void test_freshness_by_record_count_downloads_block() {
    const std::string sync_token = "sync-token";
    sync_test_support::RunningSyncPeer peer(sync_token);
    const auto key = sync_test_support::make_test_key();
    peer.adapter().set_block(key, 200, 10, "remote-more-records");

    sync_test_support::ScriptedAdapter local_adapter;
    local_adapter.set_block(key, 200, 5, "local-fewer-records");

    const auto storage_root = sync_test_support::make_temp_dir("dfh-node-peer-sync-count");
    std::filesystem::create_directories(storage_root);
    auto cfg = sync_test_support::make_client_config(peer.base_url(), sync_token, storage_root);
    dfh_node::PeerSyncService service(local_adapter, cfg, nullptr);
    service.sync_once();

    CHECK_EQ(service.blocks_downloaded_total(), 1U);
    CHECK_EQ(local_adapter.record_count_for(key), static_cast<std::size_t>(10));
    CHECK_EQ(local_adapter.marker_for(key), "remote-more-records");
    cleanup_path(storage_root);
}

void test_equal_hash_skips_download() {
    const std::string sync_token = "sync-token";
    sync_test_support::RunningSyncPeer peer(sync_token);
    const auto key = sync_test_support::make_test_key();
    peer.adapter().set_block(key, 200, 10, "same-payload");

    sync_test_support::ScriptedAdapter local_adapter;
    local_adapter.set_block(key, 200, 10, "same-payload");

    const auto storage_root = sync_test_support::make_temp_dir("dfh-node-peer-sync-skip");
    std::filesystem::create_directories(storage_root);
    auto cfg = sync_test_support::make_client_config(peer.base_url(), sync_token, storage_root);
    dfh_node::PeerSyncService service(local_adapter, cfg, nullptr);
    service.sync_once();

    CHECK_EQ(service.blocks_downloaded_total(), 0U);
    CHECK_EQ(service.blocks_skipped_total(), 1U);
    CHECK_EQ(service.divergence_total(), 0U);
    CHECK_EQ(local_adapter.merge_calls(), static_cast<std::size_t>(0));
    cleanup_path(storage_root);
}

void test_divergence_downloads_and_updates_block() {
    const std::string sync_token = "sync-token";
    sync_test_support::RunningSyncPeer peer(sync_token);
    const auto key = sync_test_support::make_test_key();
    peer.adapter().set_block(key, 200, 10, "remote-divergent");

    sync_test_support::ScriptedAdapter local_adapter;
    local_adapter.set_block(key, 200, 10, "local-divergent");

    const auto storage_root = sync_test_support::make_temp_dir("dfh-node-peer-sync-divergence");
    std::filesystem::create_directories(storage_root);
    auto cfg = sync_test_support::make_client_config(peer.base_url(), sync_token, storage_root);
    dfh_node::PeerSyncService service(local_adapter, cfg, nullptr);
    service.sync_once();

    CHECK_EQ(service.divergence_total(), 1U);
    CHECK_EQ(service.blocks_downloaded_total(), 1U);
    CHECK_EQ(local_adapter.marker_for(key), "remote-divergent");
    cleanup_path(storage_root);
}

void test_start_shutdown_and_is_running() {
    sync_test_support::ScriptedAdapter local_adapter;
    auto cfg = dfh_node::config::default_config();
    cfg.node_id = "sync-client-node";
    cfg.env = "test";
    cfg.security.server_secret = "0123456789abcdef0123456789abcdef";
    cfg.sync.enabled = true;
    cfg.sync.pull_interval_ms = 50;

    dfh_node::PeerSyncService service(local_adapter, cfg, nullptr);
    CHECK(!service.is_running());
    service.start();
    CHECK(service.is_running());
    service.shutdown();
    CHECK(!service.is_running());
}

void test_disk_low_skips_merge_and_counts_error() {
    const std::string sync_token = "sync-token";
    sync_test_support::RunningSyncPeer peer(sync_token);
    const auto key = sync_test_support::make_test_key();
    peer.adapter().set_block(key, 200, 10, "remote-disk-low");

    sync_test_support::ScriptedAdapter local_adapter;
    const auto storage_root = sync_test_support::make_temp_dir("dfh-node-peer-sync-disk-low");
    std::filesystem::create_directories(storage_root);
    auto cfg = sync_test_support::make_client_config(peer.base_url(), sync_token, storage_root);
    dfh_node::DiskMonitor disk_monitor(storage_root.string(), std::numeric_limits<std::uint64_t>::max());
    dfh_node::PeerSyncService service(local_adapter, cfg, &disk_monitor);
    service.sync_once();

    CHECK_EQ(service.sync_errors_total(), 1U);
    CHECK_EQ(service.blocks_downloaded_total(), 0U);
    CHECK_EQ(local_adapter.merge_calls(), static_cast<std::size_t>(0));
    CHECK(!local_adapter.has_block(key));
    cleanup_path(storage_root);
}

void test_attempt_and_success_timestamps_are_separated() {
    sync_test_support::ScriptedAdapter local_adapter;
    const auto storage_root = sync_test_support::make_temp_dir("dfh-node-peer-sync-attempt");
    std::filesystem::create_directories(storage_root);

    const unsigned short unused_port = sync_test_support::acquire_free_port();
    auto cfg = sync_test_support::make_client_config("http://127.0.0.1:" + std::to_string(unused_port), "sync-token",
                                                     storage_root);
    cfg.sync.request_timeout_ms = 200;

    dfh_node::PeerSyncService service(local_adapter, cfg, nullptr);
    CHECK_EQ(service.last_attempt_at_ms(), static_cast<std::int64_t>(0));
    CHECK_EQ(service.last_success_at_ms(), static_cast<std::int64_t>(0));

    service.sync_once();

    CHECK(service.last_attempt_at_ms() > 0);
    CHECK_EQ(service.last_success_at_ms(), static_cast<std::int64_t>(0));
    CHECK(service.sync_errors_total() >= 1U);
    cleanup_path(storage_root);
}

} // namespace

int main() {
    test_freshness_by_last_ts_downloads_block();
    test_freshness_by_record_count_downloads_block();
    test_equal_hash_skips_download();
    test_divergence_downloads_and_updates_block();
    test_start_shutdown_and_is_running();
    test_disk_low_skips_merge_and_counts_error();
    test_attempt_and_success_timestamps_are_separated();
    return 0;
}
