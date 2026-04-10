/// \file logging.cpp
/// \brief Реализация инициализации логирования проекта.
/// \details Настраивает `log-it-cpp` по конфигурации и не дублирует
/// инициализацию логгеров при повторных вызовах.

#include "logging.hpp"

#include "version.hpp"

#include <LogIt.hpp>

#include <cctype>
#include <exception>
#include <iostream>
#include <string>

namespace dfh_node::logging {
namespace {

/// \brief Преобразует строковый уровень логирования без учёта регистра.
/// \param level Строковое значение уровня из конфигурации.
/// \return Соответствующий `logit::LogLevel`.
logit::LogLevel parse_level(const std::string &level) {
    std::string lower;
    lower.reserve(level.size());
    for (char ch : level) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    if (lower == "trace") {
        return logit::LogLevel::LOG_LVL_TRACE;
    }
    if (lower == "debug") {
        return logit::LogLevel::LOG_LVL_DEBUG;
    }
    if (lower == "info") {
        return logit::LogLevel::LOG_LVL_INFO;
    }
    if (lower == "warn") {
        return logit::LogLevel::LOG_LVL_WARN;
    }
    if (lower == "error") {
        return logit::LogLevel::LOG_LVL_ERROR;
    }
    return logit::LogLevel::LOG_LVL_INFO;
}

} // namespace

void init_logging(const config::LoggingConfig &log_cfg) {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    if (log_cfg.console) {
        LOGIT_ADD_CONSOLE_DEFAULT();
    }

    if (!log_cfg.file_path.empty()) {
        try {
            LOGIT_ADD_FILE_LOGGER(log_cfg.file_path, true, LOGIT_FILE_LOGGER_AUTO_DELETE_DAYS,
                                  LOGIT_FILE_LOGGER_PATTERN);
        } catch (const std::exception &ex) {
            if (log_cfg.console) {
                LOGIT_PRINTF_WARN("Failed to init file logger: %s", ex.what());
            } else {
                std::cerr << "Failed to init file logger: " << ex.what() << "\n";
            }
        }
    }

    LOGIT_SET_LOG_LEVEL(parse_level(log_cfg.level));
}

void log_startup_status(const config::Config &cfg, const StatusSnapshot &status) {
    DFH_PRINTF_INFO("dfh-node v%s starting...", std::string(version()).c_str());
    DFH_PRINTF_INFO("Node ID: %s", cfg.node_id.c_str());
    DFH_PRINTF_INFO("Environment: %s", cfg.env.c_str());
    DFH_PRINTF_INFO("Status: node_id=%s, version=%s, build=%s, uptime_ms=%llu, peers_count=%llu, env=%s",
                    status.node_id.c_str(), status.version.c_str(), status.build_info.c_str(),
                    static_cast<unsigned long long>(status.uptime_ms),
                    static_cast<unsigned long long>(status.peers_count), status.env.c_str());
    DFH_PRINTF_INFO("Queues: high(size=%zu, cap=%zu, rej=%llu, drop=%llu, enq=%llu, proc=%llu, wait=%.2fms), "
                    "low(size=%zu, cap=%zu, rej=%llu, drop=%llu, enq=%llu, proc=%llu, wait=%.2fms), workers=%d",
                    status.high_priority_queue.current_size, status.high_priority_queue.capacity,
                    static_cast<unsigned long long>(status.high_priority_queue.rejected_count),
                    static_cast<unsigned long long>(status.high_priority_queue.dropped_count),
                    static_cast<unsigned long long>(status.high_priority_queue.total_enqueued),
                    static_cast<unsigned long long>(status.high_priority_queue.total_processed),
                    status.high_priority_queue.avg_wait_ms, status.low_priority_queue.current_size,
                    status.low_priority_queue.capacity,
                    static_cast<unsigned long long>(status.low_priority_queue.rejected_count),
                    static_cast<unsigned long long>(status.low_priority_queue.dropped_count),
                    static_cast<unsigned long long>(status.low_priority_queue.total_enqueued),
                    static_cast<unsigned long long>(status.low_priority_queue.total_processed),
                    status.low_priority_queue.avg_wait_ms, status.workers_count);
}

void log_http_server_started(const config::HttpConfig &cfg) {
    DFH_PRINTF_INFO("HTTP server is running on %s:%d", cfg.bind_host.c_str(), cfg.port);
}

void log_ws_server_started(const config::WsConfig &cfg) {
    DFH_PRINTF_INFO("WS server is running on %s:%d", cfg.bind_host.c_str(), cfg.port);
}

void log_ws_server_disabled() { DFH_INFO("WS server is disabled (ws.port=0)"); }

void log_runtime_bootstrap_error(const char *message) { DFH_PRINTF_ERROR("%s", message); }

void log_gate_reject(const char *code, const char *transport, const char *kind, const char *fingerprint) {
    DFH_WARN("[gate_reject] code=", (code ? code : "unknown"), " transport=", (transport ? transport : "unknown"),
             " kind=", (kind ? kind : "unknown"), " fingerprint=", (fingerprint ? fingerprint : "unknown"));
}

} // namespace dfh_node::logging
