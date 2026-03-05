/// \file ws_runtime_utils.cpp
/// \brief Реализация переиспользуемых утилит WS runtime-слоя.
/// \details Инкапсулирует общие операции для `WsMessageHandler` и `WsRouter`.
///
#include "ws_runtime_utils.hpp"

#include <chrono>
#include <utility>

namespace dfh_node::transport {

std::uint64_t steady_now_ms() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

Task make_task(const TaskKind kind, std::string request_id, std::function<void()> payload) {
    Task task;
    task.kind = kind;
    task.request_id = std::move(request_id);
    task.enqueue_ts_ms = steady_now_ms();
    task.payload = std::move(payload);
    return task;
}

std::string ws_op_to_string(const WsOp op) {
    switch (op) {
    case WsOp::Ingest:
        return "ingest";
    case WsOp::History:
        return "history";
    case WsOp::Subscribe:
        return "subscribe";
    }

    return "history";
}

std::string gate_error_close_reason(const GateError &error) {
    switch (error.code) {
    case GateErrorCode::Unauthorized:
        return "unauthorized";
    case GateErrorCode::Forbidden:
        return "forbidden";
    case GateErrorCode::RateLimited:
        return "rate_limited";
    case GateErrorCode::ConnectionLimited:
        return "connection_limited";
    default:
        return "forbidden";
    }
}

} // namespace dfh_node::transport
