/**
 * \file main.cpp
 * \brief Точка входа dfh_node_app и базовая инициализация.
 * \details Читает конфигурацию, валидирует и выводит стартовый статус.
 */
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <LogIt.hpp>

#include "build_info.hpp"
#include "config_loader.hpp"
#include "config_validator.hpp"
#include "logging.hpp"
#include "status.hpp"
#include "task_scheduler.hpp"
#include "version.hpp"
#include "worker_pool.hpp"

namespace {

// Печатает подсказку по аргументам командной строки.
void print_usage() { std::cerr << "Usage: dfh_node_app --config <path>\n"; }

// Преобразует уровень логирования из строки, игнорируя регистр.
logit::LogLevel parse_level(const std::string &level) {
    std::string lower;
    lower.reserve(level.size());
    for (char ch : level) {
        // Приводим к unsigned char, чтобы избежать UB на отрицательных значениях.
        lower.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
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

// Ищет аргумент --config и возвращает следующий за ним путь.
std::string find_config_path(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config") {
            if (i + 1 >= argc) {
                return {};
            }
            return argv[i + 1];
        }
    }
    return {};
}

// Печатает ошибки загрузки конфигурации.
void print_errors(const std::vector<dfh_node::config::LoadError> &errors) {
    std::cerr << "Configuration errors:\n";
    for (const auto &err : errors) {
        std::cerr << "- [" << err.path << "] " << err.code << ": "
                  << err.message << "\n";
    }
}

// Печатает ошибки валидации конфигурации.
void print_errors(
    const std::vector<dfh_node::config::ValidationError> &errors) {
    std::cerr << "Configuration errors:\n";
    for (const auto &err : errors) {
        std::cerr << "- [" << err.path << "] " << err.code << ": "
                  << err.message << "\n";
    }
}

} // namespace

namespace dfh_node::logging {

// Инициализация должна быть идемпотентной, чтобы не плодить логгеры.
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
            LOGIT_ADD_FILE_LOGGER(log_cfg.file_path, true,
                                  LOGIT_FILE_LOGGER_AUTO_DELETE_DAYS,
                                  LOGIT_FILE_LOGGER_PATTERN);
        } catch (const std::exception &ex) {
            // Если файл не открылся, предупреждаем через доступный канал.
            if (log_cfg.console) {
                LOGIT_PRINTF_WARN("Failed to init file logger: %s", ex.what());
            } else {
                std::cerr << "Failed to init file logger: " << ex.what()
                          << "\n";
            }
        }
    }

    LOGIT_SET_LOG_LEVEL(parse_level(log_cfg.level));
}

} // namespace dfh_node::logging

int main(int argc, char **argv) {
    const std::string config_path = find_config_path(argc, argv);
    if (config_path.empty()) {
        print_usage();
        return 1;
    }

    const auto result =
        dfh_node::config::load_from_file(std::filesystem::path(config_path));
    if (!result.is_ok()) {
        print_errors(result.errors);
        return 1;
    }

    const auto validation_errors = dfh_node::config::validate(*result.config);
    if (!validation_errors.empty()) {
        print_errors(validation_errors);
        return 1;
    }

    dfh_node::TaskScheduler scheduler(
        result.config->queues.high_capacity,
        result.config->queues.low_capacity);

    dfh_node::WorkerPool pool(
        static_cast<std::size_t>(result.config->queues.workers), scheduler);

    pool.start();

    dfh_node::logging::init_logging(result.config->logging);

    DFH_PRINTF_INFO("dfh-node v%s starting...",
                    std::string(dfh_node::version()).c_str());
    DFH_PRINTF_INFO("Node ID: %s", result.config->node_id.c_str());
    DFH_PRINTF_INFO("Environment: %s", result.config->env.c_str());

    dfh_node::StatusSnapshot status;
    status.node_id = result.config->node_id;
    status.version = std::string(dfh_node::version());
    status.build_info = dfh_node::build_info_string();
    status.uptime_ms = 0;
    status.peers_count = result.config->peers.size();
    status.env = result.config->env;

    auto high_metrics = scheduler.high_metrics();
    high_metrics.total_processed =
        pool.total_processed(dfh_node::TaskLane::High);
    high_metrics.avg_wait_ms =
        pool.avg_wait_ms(dfh_node::TaskLane::High);
    status.high_priority_queue = high_metrics;

    auto low_metrics = scheduler.low_metrics();
    low_metrics.total_processed =
        pool.total_processed(dfh_node::TaskLane::Low);
    low_metrics.avg_wait_ms =
        pool.avg_wait_ms(dfh_node::TaskLane::Low);
    status.low_priority_queue = low_metrics;

    status.workers_count = result.config->queues.workers;

    DFH_PRINTF_INFO("Status: node_id=%s, version=%s, build=%s, uptime_ms=%llu, "
                    "peers_count=%llu, env=%s",
                    status.node_id.c_str(), status.version.c_str(),
                    status.build_info.c_str(),
                    static_cast<unsigned long long>(status.uptime_ms),
                    static_cast<unsigned long long>(status.peers_count),
                    status.env.c_str());

    DFH_PRINTF_INFO(
        "Queues: high(size=%zu, cap=%zu, rej=%llu, drop=%llu, enq=%llu, "
        "proc=%llu, wait=%.2fms), low(size=%zu, cap=%zu, rej=%llu, "
        "drop=%llu, enq=%llu, proc=%llu, wait=%.2fms), workers=%d",
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

    // One-shot mode: остановить воркеры (graceful).
    // TODO: daemon mode / event-loop для долгоживущего процесса (при HTTP/WS транспорте).
    // TODO: signal handler для Ctrl+C (при долгоживущем режиме через флаг --run).
    // TODO: режим --print-status-json для машинного чтения статуса.
    pool.shutdown();

    return 0;
}
