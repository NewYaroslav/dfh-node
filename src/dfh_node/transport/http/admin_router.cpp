/// \file admin_router.cpp
/// \brief Реализация роутера HTTP Admin API.
/// \details Содержит синхронные CRUD-обработчики для динамических API-ключей
/// в MDBX.
///
#include "admin_router.hpp"

#include "admin_dto_parser.hpp"
#include "http_error_map.hpp"
#include "security/anti_replay_fields.hpp"
#include "security/sha256_utils.hpp"
#include "transport/transport_security_utils.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace dfh_node::transport {
namespace {

void send_response(const AdminRouter::HttpResponse &response, const int status, const std::string &body,
                   const std::string &content_type, SimpleWeb::CaseInsensitiveMultimap extra_headers = {}) {
    extra_headers.emplace("Content-Type", content_type);
    response->write(static_cast<SimpleWeb::StatusCode>(status), body, extra_headers);
    response->send();
}

void send_empty_response(const AdminRouter::HttpResponse &response, const int status) {
    response->write(static_cast<SimpleWeb::StatusCode>(status), "");
    response->send();
}

void send_json(const AdminRouter::HttpResponse &response, const int status, const nlohmann::json &body) {
    send_response(response, status, body.dump(), "application/json");
}

void send_error_code(const AdminRouter::HttpResponse &response, const std::string_view error_code,
                     const std::string_view detail = "") {
    const auto [status, body] = error_code_to_http(error_code, detail);
    send_response(response, status, body, "application/json");
}

void send_gate_error(const AdminRouter::HttpResponse &response, const GateError &error) {
    const auto [status, body] = gate_error_to_http(error);
    send_response(response, status, body, "application/json");
}

bool authorize_request(UnifiedGate &gate, const AdminRouter::HttpRequest &request,
                       const AdminRouter::HttpResponse &resp, const bool require_anti_replay,
                       const std::string &body_hash) {
    const std::string token = extract_bearer_token(request->header);

    HttpAntiReplayFields ar_fields;
    bool has_any_ar_headers = false;
    const HttpAntiReplayFields *ar_ptr =
        parse_http_anti_replay_fields(request->method, request->path, request->query_string, request->header, body_hash,
                                      ar_fields, has_any_ar_headers);

    if (require_anti_replay && ar_ptr == nullptr) {
        send_error_code(resp, "missing_anti_replay_headers", "admin mutation requires anti-replay headers");
        return false;
    }

    if (has_any_ar_headers && ar_ptr == nullptr) {
        send_error_code(resp, "missing_anti_replay_headers", "incomplete anti-replay headers");
        return false;
    }

    const GateResult gate_result = gate.authorize_http(token, TaskKind::Admin, ar_ptr);
    if (const auto *error = std::get_if<GateError>(&gate_result)) {
        send_gate_error(resp, *error);
        return false;
    }

    if (!std::holds_alternative<AuthContext>(gate_result)) {
        send_error_code(resp, "internal_error", "unexpected gate result");
        return false;
    }

    return true;
}

std::vector<std::string> scope_mask_to_json(const ScopeMask scope_mask) {
    std::vector<std::string> scopes;
    if ((scope_mask & to_scope_mask(Scope::Read)) != 0) {
        scopes.emplace_back("read");
    }
    if ((scope_mask & to_scope_mask(Scope::Write)) != 0) {
        scopes.emplace_back("write");
    }
    if ((scope_mask & to_scope_mask(Scope::Admin)) != 0) {
        scopes.emplace_back("admin");
    }
    if ((scope_mask & to_scope_mask(Scope::Sync)) != 0) {
        scopes.emplace_back("sync");
    }
    return scopes;
}

nlohmann::json record_to_json(const MdbxKeyRecord &record) {
    nlohmann::json body;
    body["id"] = record.id;
    body["name"] = record.name;
    body["fingerprint"] = record.fingerprint;
    body["scopes"] = scope_mask_to_json(record.scope_mask);
    body["rps_limit"] = record.rps_limit;
    body["ws_max_connections"] = record.ws_max_connections;
    if (record.expires_at_ms.has_value()) {
        body["expires_at_ms"] = *record.expires_at_ms;
    } else {
        body["expires_at_ms"] = nullptr;
    }
    body["revoked"] = record.revoked;
    body["created_at_ms"] = record.created_at_ms;
    body["updated_at_ms"] = record.updated_at_ms;
    return body;
}

bool status_all_requested(const AdminRouter::HttpRequest &request) {
    std::string query = request->query_string;
    if (!query.empty() && query.front() == '?') {
        query.erase(query.begin());
    }

    const auto params = SimpleWeb::QueryString::parse(query);
    const auto it = params.find("status");
    return it != params.end() && it->second == "all";
}

std::string extract_id_from_path(const AdminRouter::HttpRequest &request) {
    if (request->path_match.size() < 2) {
        return {};
    }
    return request->path_match[1].str();
}

nlohmann::json parse_json_body(const std::string &body) { return nlohmann::json::parse(body); }

dfh_node::UpdateKeyRequest to_manager_request(const transport::UpdateKeyRequest &request) {
    dfh_node::UpdateKeyRequest manager_request;
    manager_request.name = request.name;
    manager_request.scope_mask = request.scope_mask;
    manager_request.rps_limit = request.rps_limit;
    manager_request.ws_max_connections = request.ws_max_connections;
    manager_request.expires_at_ms = request.expires_at_ms;
    return manager_request;
}

} // namespace

AdminRouter::AdminRouter(UnifiedGate &gate, ApiKeyManager &manager, const config::Config &cfg)
    : m_gate(gate), m_manager(manager), m_cfg(cfg) {}

void AdminRouter::register_all(SimpleWeb::Server<SimpleWeb::HTTP> &server) {
    (void)m_cfg;

    server.resource["^/v1/admin/keys$"]["GET"] = [this](const HttpResponse &response, const HttpRequest &request) {
        handle_list(request, response);
    };
    server.resource["^/v1/admin/keys$"]["POST"] = [this](const HttpResponse &response, const HttpRequest &request) {
        handle_create(request, response);
    };
    server.resource["^/v1/admin/keys/([^/]+)$"]["GET"] =
        [this](const HttpResponse &response, const HttpRequest &request) { handle_get(request, response); };
    server.resource["^/v1/admin/keys/([^/]+)$"]["PUT"] =
        [this](const HttpResponse &response, const HttpRequest &request) { handle_update(request, response); };
    server.resource["^/v1/admin/keys/([^/]+)/revoke$"]["POST"] =
        [this](const HttpResponse &response, const HttpRequest &request) { handle_revoke(request, response); };
    server.resource["^/v1/admin/keys/([^/]+)$"]["DELETE"] =
        [this](const HttpResponse &response, const HttpRequest &request) { handle_delete(request, response); };
}

void AdminRouter::handle_list(HttpRequest req, HttpResponse resp) {
    if (!authorize_request(m_gate, req, resp, false, compute_sha256_hex(""))) {
        return;
    }

    const std::vector<MdbxKeyRecord> records = m_manager.list(status_all_requested(req));
    nlohmann::json body = nlohmann::json::array();
    for (const MdbxKeyRecord &record : records) {
        body.push_back(record_to_json(record));
    }
    send_json(resp, 200, body);
}

void AdminRouter::handle_create(HttpRequest req, HttpResponse resp) {
    const std::string body = req->content.string();
    if (!authorize_request(m_gate, req, resp, true, compute_sha256_hex(body))) {
        return;
    }

    try {
        const nlohmann::json json_body = parse_json_body(body);
        const auto parsed = parse_create_key_request(json_body);
        if (const auto *error = std::get_if<DtoParseError>(&parsed)) {
            send_error_code(resp, "invalid_query_param", error->detail);
            return;
        }

        const auto &request = std::get<CreateKeyRequest>(parsed);
        const CreateKeyResult created = m_manager.create(request.name, request.scope_mask, request.rps_limit,
                                                         request.ws_max_connections, request.expires_at_ms);

        nlohmann::json response_body = record_to_json(created.record);
        response_body["token"] = created.token;
        send_json(resp, 201, response_body);
    } catch (const nlohmann::json::parse_error &error) {
        send_error_code(resp, "invalid_json", error.what());
    } catch (const std::runtime_error &error) {
        if (std::string_view(error.what()) == "duplicate name") {
            send_error_code(resp, "duplicate_name", error.what());
            return;
        }
        send_error_code(resp, "internal_error", error.what());
    } catch (const std::exception &error) {
        send_error_code(resp, "internal_error", error.what());
    }
}

void AdminRouter::handle_get(HttpRequest req, HttpResponse resp) {
    if (!authorize_request(m_gate, req, resp, false, compute_sha256_hex(""))) {
        return;
    }

    const std::string id = extract_id_from_path(req);
    const std::optional<MdbxKeyRecord> record = m_manager.get_by_id(id);
    if (!record.has_value()) {
        send_error_code(resp, "not_found", "api key not found");
        return;
    }

    send_json(resp, 200, record_to_json(*record));
}

void AdminRouter::handle_update(HttpRequest req, HttpResponse resp) {
    const std::string body = req->content.string();
    if (!authorize_request(m_gate, req, resp, true, compute_sha256_hex(body))) {
        return;
    }

    try {
        const nlohmann::json json_body = parse_json_body(body);
        const auto parsed = parse_update_key_request(json_body);
        if (const auto *error = std::get_if<DtoParseError>(&parsed)) {
            send_error_code(resp, "invalid_query_param", error->detail);
            return;
        }

        const std::string id = extract_id_from_path(req);
        const transport::UpdateKeyRequest &request = std::get<transport::UpdateKeyRequest>(parsed);
        if (!m_manager.update(id, to_manager_request(request))) {
            send_error_code(resp, "not_found", "api key not found");
            return;
        }

        const std::optional<MdbxKeyRecord> updated = m_manager.get_by_id(id);
        if (!updated.has_value()) {
            send_error_code(resp, "not_found", "api key not found");
            return;
        }

        send_json(resp, 200, record_to_json(*updated));
    } catch (const nlohmann::json::parse_error &error) {
        send_error_code(resp, "invalid_json", error.what());
    } catch (const std::runtime_error &error) {
        if (std::string_view(error.what()) == "duplicate name") {
            send_error_code(resp, "duplicate_name", error.what());
            return;
        }
        send_error_code(resp, "internal_error", error.what());
    } catch (const std::exception &error) {
        send_error_code(resp, "internal_error", error.what());
    }
}

void AdminRouter::handle_revoke(HttpRequest req, HttpResponse resp) {
    if (!authorize_request(m_gate, req, resp, true, compute_sha256_hex(""))) {
        return;
    }

    const std::string id = extract_id_from_path(req);
    bool already_revoked = false;
    if (!m_manager.revoke(id, already_revoked)) {
        send_error_code(resp, "not_found", "api key not found");
        return;
    }

    nlohmann::json body;
    body["revoked"] = true;
    body["already_revoked"] = already_revoked;
    send_json(resp, 200, body);
}

void AdminRouter::handle_delete(HttpRequest req, HttpResponse resp) {
    if (!authorize_request(m_gate, req, resp, true, compute_sha256_hex(""))) {
        return;
    }

    const std::string id = extract_id_from_path(req);
    if (!m_manager.remove(id)) {
        send_error_code(resp, "not_found", "api key not found");
        return;
    }

    send_empty_response(resp, 204);
}

} // namespace dfh_node::transport
