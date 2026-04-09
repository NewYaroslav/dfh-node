/// \file test_ws_hardening.cpp
/// \brief Hardening-тесты WS transport.
/// \details Фиксирует upgrade/auth, structure/corpus, dfhbin и anti-replay
/// негативные сценарии для `/ws/json` и `/ws/msgpack`.
///
#include "transport_test_utils.hpp"

namespace {

using test_support::make_dfhbin_control;
using test_support::make_history_control;
using test_support::make_ingest_structured_control;
using test_support::make_ws_base_config;
using test_support::RunningWsNode;
using test_support::send_msgpack_control;
using test_support::sign_ws_control;
using test_support::TestWsClient;
using test_support::wait_json_response;
using test_support::wait_msgpack_response;

void expect_ws_error(const nlohmann::json &response, const char *msg_id, const char *error_code) {
    CHECK_EQ(response.at("ok").get<bool>(), false);
    CHECK_EQ(response.at("msg_id").get<std::string>(), std::string(msg_id));
    CHECK_EQ(response.at("error_code").get<std::string>(), std::string(error_code));
}

void test_upgrade_auth_failures() {
    auto cfg = make_ws_base_config();
    RunningWsNode node(std::move(cfg), "token-ws-upgrade");

    {
        TestWsClient client(node.endpoint("/ws/json"), "");
        const auto close = client.wait_close();
        CHECK(close.has_value());
        CHECK_EQ(close->status, 1008);
    }

    {
        TestWsClient client(node.endpoint("/ws/json"), "invalid-token");
        const auto close = client.wait_close();
        CHECK(close.has_value());
        CHECK_EQ(close->status, 1008);
        CHECK_NE(close->reason.find("unauthorized"), std::string::npos);
    }
}

void test_control_message_structure() {
    auto cfg = make_ws_base_config();
    RunningWsNode node(std::move(cfg), "token-ws-structure");

    {
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(std::string("\x00\x01\x02", 3));
        expect_ws_error(wait_json_response(client), "", "invalid_control_message");
    }

    {
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text("[]");
        expect_ws_error(wait_json_response(client), "", "invalid_control_message");
    }

    {
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(R"({"msg_id":"x"})");
        expect_ws_error(wait_json_response(client), "", "invalid_control_message");
    }

    {
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(R"({"op":"history"})");
        expect_ws_error(wait_json_response(client), "", "invalid_control_message");
    }

    {
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(R"({"op":"","msg_id":"empty-op"})");
        expect_ws_error(wait_json_response(client), "", "unknown_op");
    }

    {
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(R"({"op":"INGEST","msg_id":"upper-op"})");
        expect_ws_error(wait_json_response(client), "", "unknown_op");
    }

    {
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(R"({"op":"subscribe","msg_id":"sub-1","payload":{}})");
        expect_ws_error(wait_json_response(client), "sub-1", "unsupported_operation");
    }

    {
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        client.send_text(R"({"op":"history","msg_id":"bad-payload","payload":null})");
        expect_ws_error(wait_json_response(client), "", "invalid_control_message");
    }

    {
        TestWsClient client(node.endpoint("/ws/json"), node.token());
        CHECK(client.wait_open());
        const std::string long_msg_id(10 * 1024, 'm');
        nlohmann::json control = {{"op", "subscribe"}, {"msg_id", long_msg_id}, {"payload", nlohmann::json::object()}};
        client.send_text(control.dump());
        const auto response = wait_json_response(client);
        CHECK_EQ(response.at("msg_id").get<std::string>(), long_msg_id);
        CHECK_EQ(response.at("error_code").get<std::string>(), "unsupported_operation");
    }
}

void test_dfhbin_invalid_sequences() {
    auto cfg = make_ws_base_config();
    RunningWsNode node(std::move(cfg), "token-ws-dfhbin");

    TestWsClient client(node.endpoint("/ws/json"), node.token());
    CHECK(client.wait_open());

    const std::vector<std::uint8_t> payload = {1, 2, 3, 4};
    const std::string raw(reinterpret_cast<const char *>(payload.data()), payload.size());
    const std::string payload_sha256 = dfh_node::compute_sha256_hex(raw);

    client.send_binary(payload);
    expect_ws_error(wait_json_response(client), "", "unexpected_binary_frame");

    client.send_text(make_dfhbin_control("sha-mismatch", std::string(64, 'a')).dump());
    client.send_binary(payload);
    expect_ws_error(wait_json_response(client), "sha-mismatch", "sha256_mismatch");

    client.send_text(make_dfhbin_control("double-bin", payload_sha256).dump());
    client.send_binary(payload);
    CHECK_EQ(wait_json_response(client).at("ok").get<bool>(), true);
    client.send_binary(payload);
    expect_ws_error(wait_json_response(client), "", "unexpected_binary_frame");

    client.send_text(make_dfhbin_control("missing-sha", "").dump());
    expect_ws_error(wait_json_response(client), "missing-sha", "invalid_argument");

    client.send_text(make_dfhbin_control("short-sha", "abcd").dump());
    client.send_binary(payload);
    expect_ws_error(wait_json_response(client), "short-sha", "sha256_mismatch");

    client.send_text(make_dfhbin_control("pending-invalid-text", payload_sha256).dump());
    client.send_text("{");
    expect_ws_error(wait_json_response(client), "", "invalid_control_message");
    client.send_binary(payload);
    CHECK_EQ(wait_json_response(client).at("msg_id").get<std::string>(), "pending-invalid-text");

    {
        auto small_cfg = make_ws_base_config();
        small_cfg.ws.max_payload_bytes = 3;
        RunningWsNode small_node(std::move(small_cfg), "token-ws-dfhbin-small");
        TestWsClient small_client(small_node.endpoint("/ws/json"), small_node.token());
        CHECK(small_client.wait_open());
        small_client.send_text(make_dfhbin_control("too-large", payload_sha256).dump());
        small_client.send_binary(payload);
        const auto close = small_client.wait_close(std::chrono::milliseconds(3000));
        const auto error = small_client.wait_error(std::chrono::milliseconds(3000));
        CHECK(close.has_value() || error.has_value());
    }
}

void test_anti_replay_failures() {
    auto cfg = make_ws_base_config();
    cfg.security.anti_replay.enabled = true;
    cfg.security.anti_replay.max_skew_ms = 5000;
    cfg.security.anti_replay.nonce_ttl_ms = 60000;
    cfg.security.anti_replay.nonce_capacity = 128;
    cfg.security.anti_replay.require_for_scopes = dfh_node::to_scope_mask(dfh_node::Scope::Write);
    RunningWsNode node(std::move(cfg), "token-ws-ar");

    TestWsClient client(node.endpoint("/ws/json"), node.token());
    CHECK(client.wait_open());

    {
        nlohmann::json control = make_ingest_structured_control("missing-ts");
        control["nonce"] = "0011223344556677";
        control["signature"] = std::string(64, 'a');
        control["payload_hash"] = dfh_node::compute_sha256_hex(control.at("payload").dump());
        client.send_text(control.dump());
        expect_ws_error(wait_json_response(client), "missing-ts", "anti_replay_failed");
    }

    {
        nlohmann::json control = make_ingest_structured_control("missing-nonce");
        control["timestamp"] = std::to_string(test_support::now_epoch_ms());
        control["signature"] = std::string(64, 'a');
        control["payload_hash"] = dfh_node::compute_sha256_hex(control.at("payload").dump());
        client.send_text(control.dump());
        expect_ws_error(wait_json_response(client), "missing-nonce", "anti_replay_failed");
    }

    {
        nlohmann::json control = make_ingest_structured_control("bad-signature");
        sign_ws_control(control, node.token(), "/ws/json", std::to_string(test_support::now_epoch_ms()),
                        "1111222233334444");
        control["signature"] = std::string(64, '0');
        client.send_text(control.dump());
        expect_ws_error(wait_json_response(client), "bad-signature", "anti_replay_failed");
    }

    {
        nlohmann::json control = make_ingest_structured_control("replay-1");
        sign_ws_control(control, node.token(), "/ws/json", std::to_string(test_support::now_epoch_ms()),
                        "5555666677778888");
        const std::string wire = control.dump();
        client.send_text(wire);
        CHECK_EQ(wait_json_response(client).at("ok").get<bool>(), true);
        client.send_text(wire);
        expect_ws_error(wait_json_response(client), "replay-1", "anti_replay_failed");
    }
}

void test_msgpack_and_dto_corpus() {
    auto cfg = make_ws_base_config();
    cfg.ws.history_max_range_ms = 1000;
    RunningWsNode node(std::move(cfg), "token-ws-msgpack");

    TestWsClient client(node.endpoint("/ws/msgpack"), node.token());
    CHECK(client.wait_open());

    client.send_text(std::string("\x81\xc1", 2));
    expect_ws_error(wait_msgpack_response(client), "", "invalid_control_message");

    send_msgpack_control(client, nlohmann::json::array({1, 2, 3}));
    expect_ws_error(wait_msgpack_response(client), "", "invalid_control_message");

    {
        auto control = make_ingest_structured_control("ingest-block-ts-str");
        control["payload"]["block_ts"] = "oops";
        send_msgpack_control(client, control);
        expect_ws_error(wait_msgpack_response(client), "ingest-block-ts-str", "invalid_argument");
    }

    {
        auto control = make_ingest_structured_control("ingest-bad-tf");
        control["payload"]["tf"] = "bad";
        send_msgpack_control(client, control);
        expect_ws_error(wait_msgpack_response(client), "ingest-bad-tf", "invalid_argument");
    }

    {
        auto control = make_history_control("history-bad-range");
        control["payload"]["from_ms"] = 10;
        control["payload"]["to_ms"] = 10;
        send_msgpack_control(client, control);
        expect_ws_error(wait_msgpack_response(client), "history-bad-range", "invalid_argument");
    }

    {
        auto control = make_history_control("history-range-large");
        control["payload"]["from_ms"] = 0;
        control["payload"]["to_ms"] = 2000;
        send_msgpack_control(client, control);
        expect_ws_error(wait_msgpack_response(client), "history-range-large", "range_too_large");
    }

    {
        auto control = make_history_control("history-provider-id-string");
        control["payload"]["provider_id"] = "bad";
        send_msgpack_control(client, control);
        expect_ws_error(wait_msgpack_response(client), "history-provider-id-string", "invalid_argument");
    }
}

} // namespace

int main() {
    test_upgrade_auth_failures();
    test_control_message_structure();
    test_dfhbin_invalid_sequences();
    test_anti_replay_failures();
    test_msgpack_and_dto_corpus();
    return 0;
}
