/// \file ws_protocol.cpp
/// \brief Реализация парсинга и сериализации WS control/response сообщений.
/// \details Поддерживает форматы `json` и `msgpack` с нормализацией в
/// `WsControlMessage`/`WsResponseMessage`.
///
#include "ws_protocol.hpp"

#include <msgpack.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <exception>
#include <string>
#include <utility>

namespace dfh_node::transport {
namespace {

template <typename T> ParseResult<T> make_parse_error(std::string error_code, std::string detail) {
    return ParseError{std::move(error_code), std::move(detail)};
}

bool read_required_string(const nlohmann::json &root, const char *field, std::string &out, std::string &detail) {
    if (!root.contains(field)) {
        detail = std::string("missing field: ") + field;
        return false;
    }

    const auto &value = root.at(field);
    if (!value.is_string()) {
        detail = std::string("field must be string: ") + field;
        return false;
    }

    out = value.get<std::string>();
    return true;
}

bool read_optional_string(const nlohmann::json &root, const char *field, std::string &out, std::string &detail) {
    const auto it = root.find(field);
    if (it == root.end()) {
        out.clear();
        return true;
    }

    if (!it->is_string()) {
        detail = std::string("field must be string: ") + field;
        return false;
    }

    out = it->get<std::string>();
    return true;
}

bool msgpack_object_to_json(const msgpack::object &value, nlohmann::json &out, std::string &detail) {
    switch (value.type) {
    case msgpack::type::NIL:
        out = nullptr;
        return true;

    case msgpack::type::BOOLEAN:
        out = value.via.boolean;
        return true;

    case msgpack::type::POSITIVE_INTEGER:
        out = value.via.u64;
        return true;

    case msgpack::type::NEGATIVE_INTEGER:
        out = value.via.i64;
        return true;

    case msgpack::type::FLOAT32:
    case msgpack::type::FLOAT64:
        out = value.via.f64;
        return true;

    case msgpack::type::STR:
        out = std::string(value.via.str.ptr, value.via.str.size);
        return true;

    case msgpack::type::ARRAY: {
        nlohmann::json array = nlohmann::json::array();
        for (std::uint32_t i = 0; i < value.via.array.size; ++i) {
            nlohmann::json element;
            if (!msgpack_object_to_json(value.via.array.ptr[i], element, detail)) {
                return false;
            }
            array.push_back(std::move(element));
        }

        out = std::move(array);
        return true;
    }

    case msgpack::type::MAP: {
        nlohmann::json object = nlohmann::json::object();
        for (std::uint32_t i = 0; i < value.via.map.size; ++i) {
            const auto &pair = value.via.map.ptr[i];
            if (pair.key.type != msgpack::type::STR) {
                detail = "msgpack map key must be string";
                return false;
            }

            const std::string key(pair.key.via.str.ptr, pair.key.via.str.size);
            nlohmann::json mapped_value;
            if (!msgpack_object_to_json(pair.val, mapped_value, detail)) {
                return false;
            }
            object[std::move(key)] = std::move(mapped_value);
        }

        out = std::move(object);
        return true;
    }

    case msgpack::type::BIN:
        detail = "msgpack binary value is not supported in control-message";
        return false;

    case msgpack::type::EXT:
        detail = "msgpack ext value is not supported in control-message";
        return false;
    }

    detail = "unsupported msgpack value type";
    return false;
}

template <typename Packer> void pack_json_value(Packer &packer, const nlohmann::json &value) {
    switch (value.type()) {
    case nlohmann::json::value_t::null:
        packer.pack_nil();
        return;

    case nlohmann::json::value_t::boolean:
        if (value.get<bool>()) {
            packer.pack_true();
        } else {
            packer.pack_false();
        }
        return;

    case nlohmann::json::value_t::number_integer:
        packer.pack_int64(value.get<std::int64_t>());
        return;

    case nlohmann::json::value_t::number_unsigned:
        packer.pack_uint64(value.get<std::uint64_t>());
        return;

    case nlohmann::json::value_t::number_float:
        packer.pack_double(value.get<double>());
        return;

    case nlohmann::json::value_t::string: {
        const std::string text = value.get<std::string>();
        packer.pack_str(static_cast<std::uint32_t>(text.size()));
        packer.pack_str_body(text.data(), static_cast<std::uint32_t>(text.size()));
        return;
    }

    case nlohmann::json::value_t::array: {
        packer.pack_array(static_cast<std::uint32_t>(value.size()));
        for (const auto &element : value) {
            pack_json_value(packer, element);
        }
        return;
    }

    case nlohmann::json::value_t::object:
        packer.pack_map(static_cast<std::uint32_t>(value.size()));
        for (auto it = value.begin(); it != value.end(); ++it) {
            const std::string &key = it.key();
            packer.pack_str(static_cast<std::uint32_t>(key.size()));
            packer.pack_str_body(key.data(), static_cast<std::uint32_t>(key.size()));
            pack_json_value(packer, it.value());
        }
        return;

    case nlohmann::json::value_t::binary: {
        const auto &binary = value.get_binary();
        packer.pack_bin(static_cast<std::uint32_t>(binary.size()));
        packer.pack_bin_body(reinterpret_cast<const char *>(binary.data()), static_cast<std::uint32_t>(binary.size()));
        return;
    }

    case nlohmann::json::value_t::discarded:
        packer.pack_nil();
        return;
    }
}

ParseResult<WsControlMessage> parse_control_message(const nlohmann::json &root, WsFormat format) {
    if (!root.is_object()) {
        return make_parse_error<WsControlMessage>("invalid_control_message", "control message must be object");
    }

    std::string op_text;
    std::string msg_id;
    std::string detail;
    if (!read_required_string(root, "op", op_text, detail) || !read_required_string(root, "msg_id", msg_id, detail)) {
        return make_parse_error<WsControlMessage>("invalid_control_message", detail);
    }

    const std::optional<WsOp> op = parse_ws_op(op_text);
    if (!op.has_value()) {
        return make_parse_error<WsControlMessage>("unknown_op", op_text);
    }

    nlohmann::json payload = nlohmann::json::object();
    const auto payload_it = root.find("payload");
    if (payload_it != root.end()) {
        if (!payload_it->is_object()) {
            return make_parse_error<WsControlMessage>("invalid_control_message", "field must be object: payload");
        }
        payload = *payload_it;
    }

    std::string payload_hash;
    std::string payload_sha256;
    std::string timestamp;
    std::string nonce;
    std::string signature;
    if (!read_optional_string(root, "payload_hash", payload_hash, detail) ||
        !read_optional_string(root, "payload_sha256", payload_sha256, detail) ||
        !read_optional_string(root, "timestamp", timestamp, detail) ||
        !read_optional_string(root, "nonce", nonce, detail) ||
        !read_optional_string(root, "signature", signature, detail)) {
        return make_parse_error<WsControlMessage>("invalid_control_message", detail);
    }

    WsControlMessage message;
    message.op = *op;
    message.msg_id = std::move(msg_id);
    message.payload = std::move(payload);
    message.format = format;
    message.payload_hash = std::move(payload_hash);
    message.payload_sha256 = std::move(payload_sha256);
    message.timestamp = std::move(timestamp);
    message.nonce = std::move(nonce);
    message.signature = std::move(signature);
    return message;
}

nlohmann::json response_to_json(const WsResponseMessage &msg) {
    nlohmann::json json = {{"msg_id", msg.msg_id}, {"ok", msg.ok}};
    if (msg.ok) {
        json["data"] = msg.data;
    } else {
        json["error_code"] = msg.error_code;
        json["detail"] = msg.detail;
    }
    return json;
}

} // namespace

ParseResult<WsControlMessage> parse_ws_json(const std::string &text) {
    try {
        const nlohmann::json root = nlohmann::json::parse(text);
        return parse_control_message(root, WsFormat::Json);
    } catch (const nlohmann::json::parse_error &e) {
        return make_parse_error<WsControlMessage>("invalid_control_message", e.what());
    }
}

ParseResult<WsControlMessage> parse_ws_msgpack(const std::vector<std::uint8_t> &bytes) {
    if (bytes.empty()) {
        return make_parse_error<WsControlMessage>("invalid_control_message", "msgpack parse error");
    }

    try {
        msgpack::object_handle object_handle =
            msgpack::unpack(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        nlohmann::json root;
        std::string detail;
        if (!msgpack_object_to_json(object_handle.get(), root, detail)) {
            return make_parse_error<WsControlMessage>("invalid_control_message", std::move(detail));
        }
        return parse_control_message(root, WsFormat::Msgpack);
    } catch (const std::exception &) {
        return make_parse_error<WsControlMessage>("invalid_control_message", "msgpack parse error");
    }
}

std::string serialize_ws_json_response(const WsResponseMessage &msg) { return response_to_json(msg).dump(); }

std::vector<std::uint8_t> serialize_ws_msgpack_response(const WsResponseMessage &msg) {
    msgpack::sbuffer buffer;
    msgpack::packer<msgpack::sbuffer> packer(buffer);
    pack_json_value(packer, response_to_json(msg));
    const auto begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
    return {begin, begin + buffer.size()};
}

std::optional<WsOp> parse_ws_op(const std::string &op_str) {
    if (op_str == "ingest") {
        return WsOp::Ingest;
    }
    if (op_str == "history") {
        return WsOp::History;
    }
    if (op_str == "subscribe") {
        return WsOp::Subscribe;
    }
    return std::nullopt;
}

TaskKind ws_op_to_task_kind(WsOp op) {
    switch (op) {
    case WsOp::Ingest:
        return TaskKind::Ingest;
    case WsOp::History:
        return TaskKind::History;
    case WsOp::Subscribe:
        return TaskKind::History;
    }

    return TaskKind::History;
}

} // namespace dfh_node::transport
