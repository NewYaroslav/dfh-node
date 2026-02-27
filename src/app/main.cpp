/// \file main.cpp
/// \brief Точка входа dfh_node_app и базовая инициализация.
/// \details Читает конфигурацию, валидирует и выводит стартовый статус.
///
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <LogIt.hpp>

#include "adapter.hpp"
#include "auth.hpp"
#include "config.hpp"
#include "core.hpp"
#include "scheduler.hpp"
#include "security.hpp"
#include "transport.hpp"

namespace {

// Печатает подсказку по аргументам командной строки.
void print_usage() { std::cerr << "Usage: dfh_node_app --config <path> [--run]\n"; }

// Преобразует уровень логирования из строки, игнорируя регистр.
logit::LogLevel parse_level(const std::string &level) {
    std::string lower;
    lower.reserve(level.size());
    for (char ch : level) {
        // Приводим к unsigned char, чтобы избежать UB на отрицательных
        // значениях.
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

// Проверяет наличие флага в аргументах командной строки.
bool has_flag(int argc, char **argv, const std::string &flag_name) {
    for (int i = 1; i < argc; ++i) {
        if (flag_name == argv[i]) {
            return true;
        }
    }
    return false;
}

/// \brief Часы Unix epoch для anti-replay в runtime.
/// \details Отдельная реализация нужна, чтобы не менять поведение `SystemClock`
/// из существующих тестов.
class EpochSystemClock final : public dfh_node::IClock {
public:
    std::uint64_t now_ms() const override {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
    }
};

// Печатает ошибки загрузки конфигурации.
void print_errors(const std::vector<dfh_node::config::LoadError> &errors) {
    std::cerr << "Configuration errors:\n";
    for (const auto &err : errors) {
        std::cerr << "- [" << err.path << "] " << err.code << ": " << err.message << "\n";
    }
}

// Печатает ошибки валидации конфигурации.
void print_errors(const std::vector<dfh_node::config::ValidationError> &errors) {
    std::cerr << "Configuration errors:\n";
    for (const auto &err : errors) {
        std::cerr << "- [" << err.path << "] " << err.code << ": " << err.message << "\n";
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
            LOGIT_ADD_FILE_LOGGER(log_cfg.file_path, true, LOGIT_FILE_LOGGER_AUTO_DELETE_DAYS,
                                  LOGIT_FILE_LOGGER_PATTERN);
        } catch (const std::exception &ex) {
            // Если файл не открылся, предупреждаем через доступный канал.
            if (log_cfg.console) {
                LOGIT_PRINTF_WARN("Failed to init file logger: %s", ex.what());
            } else {
                std::cerr << "Failed to init file logger: " << ex.what() << "\n";
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
    const bool run_mode = has_flag(argc, argv, "--run");

    const auto result = dfh_node::config::load_from_file(std::filesystem::path(config_path));
    if (!result.is_ok()) {
        print_errors(result.errors);
        return 1;
    }

    const auto validation_errors = dfh_node::config::validate(*result.config);
    if (!validation_errors.empty()) {
        print_errors(validation_errors);
        return 1;
    }

    const auto &cfg = *result.config;

    dfh_node::TaskScheduler scheduler(cfg.queues.high_capacity, cfg.queues.low_capacity);

    dfh_node::WorkerPool pool(static_cast<std::size_t>(cfg.queues.workers), scheduler);

    pool.start();

    dfh_node::logging::init_logging(cfg.logging);

    dfh_node::ConfigApiKeyStore api_key_store(cfg.auth.api_keys);
    dfh_node::FingerprintComputer fingerprint_computer(cfg.security.server_secret);
    dfh_node::AuthCache auth_cache(cfg.auth.cache_ttl_ms);
    dfh_node::AuthService auth_service(api_key_store, auth_cache, fingerprint_computer);
    dfh_node::RateLimiter rate_limiter(cfg.auth.rps_limit, cfg.auth.rate_limit_window_ms);
    dfh_node::WsConnectionLimiter ws_connection_limiter;
    EpochSystemClock epoch_clock;

    std::unique_ptr<dfh_node::NonceStore> nonce_store;
    std::unique_ptr<dfh_node::AntiReplayValidator> anti_replay_validator;
    if (cfg.security.anti_replay.enabled) {
        nonce_store = std::make_unique<dfh_node::NonceStore>(epoch_clock, cfg.security.anti_replay.nonce_ttl_ms,
                                                             cfg.security.anti_replay.nonce_capacity);
        anti_replay_validator =
            std::make_unique<dfh_node::AntiReplayValidator>(cfg.security.anti_replay, epoch_clock, *nonce_store);
    }

    dfh_node::UnifiedGate gate(auth_service, rate_limiter, ws_connection_limiter, anti_replay_validator.get(),
                               cfg.security.anti_replay.require_for_scopes);
    dfh_node::FakeDfhAdapter adapter;
    dfh_node::transport::HttpRouter router(gate, scheduler, adapter, cfg);
    dfh_node::transport::HttpServer http_server(cfg.http, router);

    DFH_PRINTF_INFO("dfh-node v%s starting...", std::string(dfh_node::version()).c_str());
    DFH_PRINTF_INFO("Node ID: %s", cfg.node_id.c_str());
    DFH_PRINTF_INFO("Environment: %s", cfg.env.c_str());

    dfh_node::StatusSnapshot status;
    status.node_id = cfg.node_id;
    status.version = std::string(dfh_node::version());
    status.build_info = dfh_node::build_info_string();
    status.uptime_ms = 0;
    status.peers_count = cfg.peers.size();
    status.env = cfg.env;

    auto high_metrics = scheduler.high_metrics();
    high_metrics.total_processed = pool.total_processed(dfh_node::TaskLane::High);
    high_metrics.avg_wait_ms = pool.avg_wait_ms(dfh_node::TaskLane::High);
    status.high_priority_queue = high_metrics;

    auto low_metrics = scheduler.low_metrics();
    low_metrics.total_processed = pool.total_processed(dfh_node::TaskLane::Low);
    low_metrics.avg_wait_ms = pool.avg_wait_ms(dfh_node::TaskLane::Low);
    status.low_priority_queue = low_metrics;

    status.workers_count = cfg.queues.workers;

    DFH_PRINTF_INFO("Status: node_id=%s, version=%s, build=%s, uptime_ms=%llu, "
                    "peers_count=%llu, env=%s",
                    status.node_id.c_str(), status.version.c_str(), status.build_info.c_str(),
                    static_cast<unsigned long long>(status.uptime_ms),
                    static_cast<unsigned long long>(status.peers_count), status.env.c_str());

    DFH_PRINTF_INFO("Queues: high(size=%zu, cap=%zu, rej=%llu, drop=%llu, enq=%llu, "
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

    if (run_mode) {
        try {
            http_server.start();
            DFH_PRINTF_INFO("HTTP server is running on %s:%d", cfg.http.bind_host.c_str(), cfg.http.port);
            std::cout << "Press Enter to stop dfh_node_app...\n";
            std::string line;
            std::getline(std::cin, line);
        } catch (const std::exception &ex) {
            DFH_PRINTF_ERROR("Failed to start HTTP server: %s", ex.what());
            pool.shutdown();
            return 1;
        } catch (...) {
            DFH_PRINTF_ERROR("%s", "Failed to start HTTP server: unknown error");
            pool.shutdown();
            return 1;
        }

        http_server.shutdown();
    }

    // One-shot по умолчанию: без флага --run приложение выполняет bootstrap и
    // завершается.
    pool.shutdown();

    return 0;
}
