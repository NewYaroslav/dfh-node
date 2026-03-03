/// \file ws_message_handler.cpp
/// \brief Временная реализация обработчика WS-сообщений.
/// \details На этапе 4 обработка сообщений не выполняется; методы подготовлены
/// для последующей полной реализации на этапе 5.
///
#include "ws_message_handler.hpp"

#include <utility>

namespace dfh_node::transport {

WsMessageHandler::WsMessageHandler(UnifiedGate &gate, TaskScheduler &scheduler, IDfhAdapter &adapter,
                                   const config::Config &cfg, std::shared_ptr<WsSessionRegistry> registry)
    : m_gate(gate), m_scheduler(scheduler), m_adapter(adapter), m_cfg(cfg), m_registry(std::move(registry)) {}

void WsMessageHandler::handle_text(const std::string &connection_id, const std::string &message, bool is_msgpack) {
    (void)connection_id;
    (void)message;
    (void)is_msgpack;
}

void WsMessageHandler::handle_binary(const std::string &connection_id, const std::vector<std::uint8_t> &bytes) {
    (void)connection_id;
    (void)bytes;
}

} // namespace dfh_node::transport
