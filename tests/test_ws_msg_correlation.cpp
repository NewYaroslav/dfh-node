/// \file test_ws_msg_correlation.cpp
/// \brief Тесты корреляции `msg_id` в WS transport.
/// \details Проверяет сохранение `msg_id` в успехе, ошибках и при overwrite
/// pending-состояния для `dfhbin`.
///
#include "transport_test_utils.hpp"

#include <set>

namespace {

using test_support::make_dfhbin_control;
using test_support::make_history_control;
using test_support::make_ingest_structured_control;
using test_support::make_ws_base_config;
using test_support::RunningWsNode;
using test_support::TestWsClient;
using test_support::wait_json_response;

void test_parallel_requests_preserve_msg_id() {
    auto cfg = make_ws_base_config();
    cfg.queues.workers = 4;
    RunningWsNode node(std::move(cfg), "token-ws-correlation");

    TestWsClient client(node.endpoint("/ws/json"), node.token());
    CHECK(client.wait_open());

    client.send_text(make_history_control("hist-a").dump());
    client.send_text(make_ingest_structured_control("ing-b").dump());
    client.send_text(make_history_control("hist-c").dump());

    std::set<std::string> ids;
    ids.insert(wait_json_response(client).at("msg_id").get<std::string>());
    ids.insert(wait_json_response(client).at("msg_id").get<std::string>());
    ids.insert(wait_json_response(client).at("msg_id").get<std::string>());

    CHECK_EQ(ids.size(), static_cast<std::size_t>(3));
    CHECK(ids.count("hist-a") == 1);
    CHECK(ids.count("ing-b") == 1);
    CHECK(ids.count("hist-c") == 1);
}

void test_control_and_binary_paths_keep_msg_id() {
    auto cfg = make_ws_base_config();
    RunningWsNode node(std::move(cfg), "token-ws-correlation-dfhbin");

    TestWsClient client(node.endpoint("/ws/json"), node.token());
    CHECK(client.wait_open());

    const std::vector<std::uint8_t> payload = {1, 2, 3, 4};
    const std::string raw(reinterpret_cast<const char *>(payload.data()), payload.size());
    const std::string payload_sha256 = dfh_node::compute_sha256_hex(raw);

    auto invalid_control = make_dfhbin_control("dfh-c-invalid", payload_sha256);
    invalid_control["payload"]["tf"] = "bad";
    client.send_text(invalid_control.dump());
    const auto invalid_response = wait_json_response(client);
    CHECK_EQ(invalid_response.at("ok").get<bool>(), false);
    CHECK_EQ(invalid_response.at("msg_id").get<std::string>(), "dfh-c-invalid");

    client.send_text(make_dfhbin_control("dfh-c-ok", payload_sha256).dump());
    client.send_binary(payload);
    const auto ok_response = wait_json_response(client);
    CHECK_EQ(ok_response.at("ok").get<bool>(), true);
    CHECK_EQ(ok_response.at("msg_id").get<std::string>(), "dfh-c-ok");
}

void test_error_response_contains_request_msg_id() {
    auto cfg = make_ws_base_config();
    RunningWsNode node(std::move(cfg), "token-ws-correlation-error");

    TestWsClient client(node.endpoint("/ws/json"), node.token());
    CHECK(client.wait_open());

    auto control = make_history_control("corr-error");
    control["payload"]["from_ms"] = 10;
    control["payload"]["to_ms"] = 10;
    client.send_text(control.dump());

    const auto response = wait_json_response(client);
    CHECK_EQ(response.at("ok").get<bool>(), false);
    CHECK_EQ(response.at("msg_id").get<std::string>(), "corr-error");
}

void test_pending_dfhbin_overwrite_uses_latest_msg_id() {
    auto cfg = make_ws_base_config();
    RunningWsNode node(std::move(cfg), "token-ws-correlation-pending");

    TestWsClient client(node.endpoint("/ws/json"), node.token());
    CHECK(client.wait_open());

    const std::vector<std::uint8_t> payload = {9, 8, 7, 6};
    const std::string raw(reinterpret_cast<const char *>(payload.data()), payload.size());
    const std::string payload_sha256 = dfh_node::compute_sha256_hex(raw);

    client.send_text(make_dfhbin_control("dfh-old", payload_sha256).dump());
    client.send_text(make_dfhbin_control("dfh-new", payload_sha256).dump());
    client.send_binary(payload);

    const auto response = wait_json_response(client);
    CHECK_EQ(response.at("ok").get<bool>(), true);
    CHECK_EQ(response.at("msg_id").get<std::string>(), "dfh-new");
}

} // namespace

int main() {
    test_parallel_requests_preserve_msg_id();
    test_control_and_binary_paths_keep_msg_id();
    test_error_response_contains_request_msg_id();
    test_pending_dfhbin_overwrite_uses_latest_msg_id();
    return 0;
}
