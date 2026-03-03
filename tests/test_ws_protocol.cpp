/// \file test_ws_protocol.cpp
/// \brief Unit-тесты протокольного слоя WS (`json`/`msgpack`).
/// \details Проверяет разбор control-message, сериализацию ответов и
/// преобразование `op` в `TaskKind`.
///
#include "test_helpers.hpp"
#include "transport.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using dfh_node::TaskKind;
using dfh_node::transport::ParseError;
using dfh_node::transport::WsControlMessage;
using dfh_node::transport::WsFormat;
using dfh_node::transport::WsOp;
using dfh_node::transport::WsResponseMessage;

const ParseError *as_parse_error(const dfh_node::transport::ParseResult<WsControlMessage> &result) {
    return std::get_if<ParseError>(&result);
}

void test_parse_ws_json_valid() {
    const std::string body = R"({
        "op":"ingest",
        "msg_id":"m-1",
        "payload":{"provider":"binance"},
        "payload_hash":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "payload_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        "timestamp":"1762258621000",
        "nonce":"0011223344556677",
        "signature":"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
    })";

    const auto parsed = dfh_node::transport::parse_ws_json(body);
    const auto *msg = std::get_if<WsControlMessage>(&parsed);
    CHECK(msg != nullptr);
    CHECK_EQ(msg->op, WsOp::Ingest);
    CHECK_EQ(msg->msg_id, "m-1");
    CHECK_EQ(msg->payload.at("provider").get<std::string>(), "binance");
    CHECK_EQ(msg->format, WsFormat::Json);
    CHECK_EQ(msg->payload_hash.size(), static_cast<std::size_t>(64));
    CHECK_EQ(msg->payload_sha256.size(), static_cast<std::size_t>(64));
    CHECK_EQ(msg->timestamp, "1762258621000");
    CHECK_EQ(msg->nonce, "0011223344556677");
}

void test_parse_ws_json_errors() {
    {
        const auto parsed = dfh_node::transport::parse_ws_json(R"({"op":"ingest","payload":{}})");
        const auto *error = as_parse_error(parsed);
        CHECK(error != nullptr);
        CHECK_EQ(error->first, "invalid_control_message");
        CHECK_NE(error->second.find("missing field: msg_id"), std::string::npos);
    }

    {
        const auto parsed = dfh_node::transport::parse_ws_json(R"({"op":"unknown","msg_id":"m"})");
        const auto *error = as_parse_error(parsed);
        CHECK(error != nullptr);
        CHECK_EQ(error->first, "unknown_op");
    }

    {
        const auto parsed = dfh_node::transport::parse_ws_json("{");
        const auto *error = as_parse_error(parsed);
        CHECK(error != nullptr);
        CHECK_EQ(error->first, "invalid_control_message");
    }
}

void test_parse_ws_msgpack_roundtrip() {
    const nlohmann::json source = {
        {"op", "history"},
        {"msg_id", "m-2"},
        {"payload", {{"provider", "binance"}, {"symbol", "BTCUSDT"}}},
        {"payload_hash", "1111111111111111111111111111111111111111111111111111111111111111"},
        {"timestamp", "1762258621001"},
        {"nonce", "8899aabbccddeeff"},
        {"signature", "2222222222222222222222222222222222222222222222222222222222222222"},
    };
    const std::vector<std::uint8_t> bytes = nlohmann::json::to_msgpack(source);

    const auto parsed = dfh_node::transport::parse_ws_msgpack(bytes);
    const auto *msg = std::get_if<WsControlMessage>(&parsed);
    CHECK(msg != nullptr);
    CHECK_EQ(msg->format, WsFormat::Msgpack);
    CHECK_EQ(msg->op, WsOp::History);
    CHECK_EQ(msg->msg_id, "m-2");
    CHECK_EQ(msg->payload.at("provider").get<std::string>(), "binance");
    CHECK_EQ(msg->payload.at("symbol").get<std::string>(), "BTCUSDT");
}

void test_parse_ws_op_and_mapping() {
    CHECK_EQ(dfh_node::transport::parse_ws_op("ingest"), std::optional<WsOp>(WsOp::Ingest));
    CHECK_EQ(dfh_node::transport::parse_ws_op("history"), std::optional<WsOp>(WsOp::History));
    CHECK_EQ(dfh_node::transport::parse_ws_op("subscribe"), std::optional<WsOp>(WsOp::Subscribe));
    CHECK_EQ(dfh_node::transport::parse_ws_op("foo"), std::nullopt);

    CHECK_EQ(dfh_node::transport::ws_op_to_task_kind(WsOp::Ingest), TaskKind::Ingest);
    CHECK_EQ(dfh_node::transport::ws_op_to_task_kind(WsOp::History), TaskKind::History);
}

void test_serialize_json_response() {
    WsResponseMessage ok;
    ok.msg_id = "ok-1";
    ok.ok = true;
    ok.data = {{"status", "ok"}};

    const auto ok_text = dfh_node::transport::serialize_ws_json_response(ok);
    const auto ok_json = nlohmann::json::parse(ok_text);
    CHECK_EQ(ok_json.at("msg_id").get<std::string>(), "ok-1");
    CHECK_EQ(ok_json.at("ok").get<bool>(), true);
    CHECK_EQ(ok_json.at("data").at("status").get<std::string>(), "ok");

    WsResponseMessage error;
    error.msg_id = "err-1";
    error.ok = false;
    error.error_code = "invalid_argument";
    error.detail = "bad payload";
    const auto error_text = dfh_node::transport::serialize_ws_json_response(error);
    const auto error_json = nlohmann::json::parse(error_text);
    CHECK_EQ(error_json.at("msg_id").get<std::string>(), "err-1");
    CHECK_EQ(error_json.at("ok").get<bool>(), false);
    CHECK_EQ(error_json.at("error_code").get<std::string>(), "invalid_argument");
    CHECK_EQ(error_json.at("detail").get<std::string>(), "bad payload");
}

void test_serialize_msgpack_response() {
    WsResponseMessage ok;
    ok.msg_id = "ok-2";
    ok.ok = true;
    ok.data = {{"chunks", nlohmann::json::array({1, 2, 3})}};

    const auto bytes = dfh_node::transport::serialize_ws_msgpack_response(ok);
    const auto json = nlohmann::json::from_msgpack(bytes);
    CHECK_EQ(json.at("msg_id").get<std::string>(), "ok-2");
    CHECK_EQ(json.at("ok").get<bool>(), true);
    CHECK_EQ(json.at("data").at("chunks").size(), static_cast<std::size_t>(3));
}

} // namespace

int main() {
    test_parse_ws_json_valid();
    test_parse_ws_json_errors();
    test_parse_ws_msgpack_roundtrip();
    test_parse_ws_op_and_mapping();
    test_serialize_json_response();
    test_serialize_msgpack_response();
    return 0;
}
