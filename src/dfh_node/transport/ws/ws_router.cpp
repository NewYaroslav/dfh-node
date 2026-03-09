/// \file ws_router.cpp
/// \brief Реализация роутера WS-эндпоинтов.
/// \details Регистрирует `/ws/json` и `/ws/msgpack`, выполняет авторизацию
/// `upgrade` и делегирует обработку сообщений в `WsMessageHandler`.
///
#include "ws_router.hpp"

#include "security/sha256_utils.hpp"
#include "ws_message_handler.hpp"
#include "ws_runtime_utils.hpp"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dfh_node::transport {
namespace {

std::string trim_copy(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

std::string extract_bearer_token(const SimpleWeb::CaseInsensitiveMultimap &headers) {
    const auto auth_it = headers.find("Authorization");
    if (auth_it == headers.end()) {
        return {};
    }

    const std::string auth_header = trim_copy(auth_it->second);
    constexpr std::string_view prefix = "Bearer ";
    if (auth_header.size() < prefix.size() || auth_header.compare(0, prefix.size(), prefix) != 0) {
        return {};
    }

    return trim_copy(std::string_view(auth_header).substr(prefix.size()));
}

bool is_binary_frame(const std::shared_ptr<SimpleWeb::SocketServer<SimpleWeb::WS>::InMessage> &message) {
    if (!message) {
        return false;
    }

    const std::uint8_t opcode = static_cast<std::uint8_t>(message->fin_rsv_opcode & 0x0fU);
    return opcode == 0x02U;
}

} // namespace

WsRouter::WsRouter(UnifiedGate &gate, TaskScheduler &scheduler, IDfhAdapter &adapter, const config::Config &cfg,
                   std::shared_ptr<WsSessionRegistry> registry, DiskMonitor *disk_monitor)
    : m_gate(gate), m_scheduler(scheduler), m_adapter(adapter), m_cfg(cfg), m_registry(std::move(registry)),
      m_disk_monitor(disk_monitor) {
    if (!m_registry) {
        throw std::invalid_argument("WsRouter requires non-null WsSessionRegistry");
    }
    m_handler = std::make_shared<WsMessageHandler>(m_gate, m_scheduler, m_adapter, m_cfg, m_registry, m_disk_monitor);
}

void WsRouter::register_all(SimpleWeb::SocketServer<SimpleWeb::WS> &server) {
    register_endpoint(server, "/ws/json", false);
    register_endpoint(server, "/ws/msgpack", true);
}

void WsRouter::register_endpoint(SimpleWeb::SocketServer<SimpleWeb::WS> &server, const std::string &path,
                                 const bool is_msgpack) {
    const std::string route_pattern = "^" + path + "$";
    auto &endpoint = server.endpoint[route_pattern];

    endpoint.on_open = [this, path](const std::shared_ptr<WsSessionRegistry::SwsConnection> &connection) {
        if (!connection) {
            return;
        }

        const std::string token = extract_bearer_token(connection->header);
        const GateResult gate_result = m_gate.authorize_ws_upgrade(token);
        if (const auto *gate_error = std::get_if<GateError>(&gate_result)) {
            std::clog << "WARN: WS upgrade rejected: path=" << path << ", reason=" << gate_error->message << '\n';
            connection->send_close(1008, gate_error_close_reason(*gate_error));
            return;
        }

        const auto *auth_context = std::get_if<AuthContext>(&gate_result);
        if (auth_context == nullptr) {
            std::clog << "WARN: WS upgrade rejected: path=" << path << ", reason=invalid gate result\n";
            connection->send_close(1008, "unauthorized");
            return;
        }

        WsConnectionContext context;
        context.fingerprint = auth_context->fingerprint;
        context.scope_mask = auth_context->scope_mask;
        compute_sha256_raw(token, context.signing_key);
        context.endpoint = path;
        context.connected_at = std::chrono::steady_clock::now();

        const WsSessionRegistry::ConnectionId connection_id =
            m_registry->register_connection(connection, std::move(context));
        std::clog << "DEBUG: WS connection opened: id=" << connection_id << ", path=" << path << '\n';
    };

    endpoint.on_message =
        [this, is_msgpack](const std::shared_ptr<WsSessionRegistry::SwsConnection> &connection,
                           const std::shared_ptr<SimpleWeb::SocketServer<SimpleWeb::WS>::InMessage> &message) {
            if (!connection || !message || !m_handler) {
                return;
            }

            const auto id_opt = m_registry->find_id(connection.get());
            if (!id_opt.has_value()) {
                std::clog << "WARN: WS message dropped: unknown connection\n";
                return;
            }

            const std::string connection_id = *id_opt;
            if (is_binary_frame(message)) {
                const std::string payload = message->string();
                std::vector<std::uint8_t> bytes(payload.begin(), payload.end());
                m_handler->handle_binary(connection_id, bytes);
                return;
            }

            m_handler->handle_text(connection_id, message->string(), is_msgpack);
        };

    endpoint.on_close = [this](const std::shared_ptr<WsSessionRegistry::SwsConnection> &connection, const int status,
                               const std::string &reason) {
        if (!connection) {
            return;
        }

        const auto id_opt = m_registry->find_id(connection.get());
        if (!id_opt.has_value()) {
            return;
        }

        const std::optional<WsConnectionContext> context = m_registry->get_context(*id_opt);
        m_registry->unregister_connection(*id_opt);
        if (context.has_value()) {
            m_gate.ws_connection_closed(context->fingerprint);
        }

        std::clog << "DEBUG: WS connection closed: id=" << *id_opt << ", status=" << status << ", reason=" << reason
                  << '\n';
    };

    endpoint.on_error = [this](const std::shared_ptr<WsSessionRegistry::SwsConnection> &connection,
                               const SimpleWeb::error_code &error) {
        std::clog << "WARN: WS transport error: " << error.message() << '\n';

        if (!connection) {
            return;
        }

        const auto id_opt = m_registry->find_id(connection.get());
        if (!id_opt.has_value()) {
            return;
        }

        const std::optional<WsConnectionContext> context = m_registry->get_context(*id_opt);
        m_registry->unregister_connection(*id_opt);
        if (context.has_value()) {
            m_gate.ws_connection_closed(context->fingerprint);
        }
    };
}

} // namespace dfh_node::transport
