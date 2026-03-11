/// \file dfh_node_application.cpp
/// \brief Реализация приложения `dfh_node_app`.
/// \details Содержит orchestration bootstrap/runtime, а также wiring основных
/// зависимостей ноды вне `main.cpp`.

#ifdef _WIN32
#include <winsock2.h>
#endif

#include "dfh_node_application.hpp"

#include "cli_options.hpp"

#include "adapter.hpp"
#include "auth.hpp"
#include "config.hpp"
#include "core.hpp"
#include "scheduler.hpp"
#include "security.hpp"
#include "transport.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dfh_node::app {
namespace {

/// \brief Часы Unix epoch для anti-replay runtime.
/// \details Изолированы от `SystemClock`, чтобы не менять существующие тесты.
class EpochSystemClock final : public dfh_node::IClock {
public:
    std::uint64_t now_ms() const override {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
    }
};

/// \brief Печатает ошибки загрузки конфигурации.
/// \param errors Список ошибок загрузчика.
void print_errors(const std::vector<dfh_node::config::LoadError> &errors) {
    std::cerr << "Configuration errors:\n";
    for (const auto &err : errors) {
        std::cerr << "- [" << err.path << "] " << err.code << ": " << err.message << "\n";
    }
}

/// \brief Печатает ошибки валидации конфигурации.
/// \param errors Список ошибок валидатора.
void print_errors(const std::vector<dfh_node::config::ValidationError> &errors) {
    std::cerr << "Configuration errors:\n";
    for (const auto &err : errors) {
        std::cerr << "- [" << err.path << "] " << err.code << ": " << err.message << "\n";
    }
}

/// \brief Собирает начальный снимок статуса после bootstrap.
/// \param cfg Валидированная конфигурация ноды.
/// \param scheduler Планировщик задач.
/// \param pool Пул воркеров.
/// \return Снимок статуса для стартового логирования.
dfh_node::StatusSnapshot build_startup_status(const dfh_node::config::Config &cfg, dfh_node::TaskScheduler &scheduler,
                                              dfh_node::WorkerPool &pool) {
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
    return status;
}

/// \brief Полный набор runtime-компонентов ноды.
/// \details Держит зависимости в одном объекте и корректно освобождает их при
/// выходе из приложения.
class RuntimeContext final {
public:
    /// \brief Собирает runtime-компоненты из валидированной конфигурации.
    /// \param cfg Валидированная конфигурация ноды.
    explicit RuntimeContext(const dfh_node::config::Config &cfg)
        : m_cfg(cfg), m_scheduler(cfg.queues.high_capacity, cfg.queues.low_capacity),
          m_pool(static_cast<std::size_t>(cfg.queues.workers), m_scheduler), m_config_key_store(cfg.auth.api_keys),
          m_fingerprint_computer(cfg.security.server_secret), m_auth_cache(cfg.auth.cache_ttl_ms),
          m_mdbx_store((std::filesystem::path(cfg.storage.path) / "keys.mdbx").string()),
          m_composite_key_store(m_config_key_store, m_mdbx_store),
          m_disk_monitor(cfg.storage.path, static_cast<std::uint64_t>(cfg.storage.min_free_bytes)),
          m_key_manager(m_mdbx_store, m_auth_cache, m_fingerprint_computer, cfg.auth),
          m_auth_service(m_composite_key_store, m_auth_cache, m_fingerprint_computer),
          m_rate_limiter(cfg.auth.rps_limit, cfg.auth.rate_limit_window_ms), m_ws_connection_limiter(), m_epoch_clock(),
          m_nonce_store(make_nonce_store(cfg, m_epoch_clock)),
          m_anti_replay_validator(make_anti_replay_validator(cfg, m_epoch_clock, m_nonce_store.get())),
          m_gate(m_auth_service, m_rate_limiter, m_ws_connection_limiter, m_anti_replay_validator.get(),
                 cfg.security.anti_replay.require_for_scopes),
          m_adapter(), m_ws_registry(std::make_shared<dfh_node::transport::WsSessionRegistry>()),
          m_http_router(m_gate, m_scheduler, m_adapter, cfg, &m_disk_monitor, &m_mdbx_store),
          m_http_server(cfg.http, m_http_router), m_admin_router(m_gate, m_key_manager, cfg),
          m_ops_router(m_gate, m_disk_monitor, m_mdbx_store, m_scheduler, m_pool, cfg) {
        std::filesystem::create_directories(cfg.storage.path);
        m_mdbx_store.open();
        m_admin_router.register_all(m_http_server.server());
        m_ops_router.register_all(m_http_server.server());
    }

    /// \brief Гарантированно останавливает transport и worker runtime.
    ~RuntimeContext() { shutdown(); }

    /// \brief Запускает воркеры scheduler-пула.
    void start_workers() { m_pool.start(); }

    /// \brief Запускает HTTP и при необходимости WS runtime.
    void start_transport_runtime() {
        m_http_server.start();
        dfh_node::logging::log_http_server_started(m_cfg.http);

        if (m_cfg.ws.port == 0) {
            dfh_node::logging::log_ws_server_disabled();
            return;
        }

        m_ws_router = std::make_unique<dfh_node::transport::WsRouter>(m_gate, m_scheduler, m_adapter, m_cfg,
                                                                      m_ws_registry, &m_disk_monitor);
        m_ws_server = std::make_unique<dfh_node::transport::WsServer>(m_cfg.ws, *m_ws_router);
        m_ws_server->start();
        dfh_node::logging::log_ws_server_started(m_cfg.ws);
    }

    /// \brief Ожидает ручной остановки runtime через stdin.
    void wait_for_stop_signal() const {
        std::cout << "Press Enter to stop dfh_node_app...\n";
        std::string line;
        std::getline(std::cin, line);
    }

    /// \brief Возвращает ссылку на планировщик для стартовых метрик.
    /// \return Ссылка на `TaskScheduler`.
    dfh_node::TaskScheduler &scheduler() { return m_scheduler; }

    /// \brief Возвращает ссылку на пул воркеров для стартовых метрик.
    /// \return Ссылка на `WorkerPool`.
    dfh_node::WorkerPool &pool() { return m_pool; }

private:
    /// \brief Создаёт `NonceStore` только когда anti-replay включён.
    /// \param cfg Валидированная конфигурация ноды.
    /// \param clock Часы Unix epoch для TTL и skew.
    /// \return `NonceStore` или `nullptr`.
    static std::unique_ptr<dfh_node::NonceStore> make_nonce_store(const dfh_node::config::Config &cfg,
                                                                  EpochSystemClock &clock) {
        if (!cfg.security.anti_replay.enabled) {
            return nullptr;
        }

        return std::make_unique<dfh_node::NonceStore>(clock, cfg.security.anti_replay.nonce_ttl_ms,
                                                      cfg.security.anti_replay.nonce_capacity);
    }

    /// \brief Создаёт `AntiReplayValidator` только когда anti-replay включён.
    /// \param cfg Валидированная конфигурация ноды.
    /// \param clock Часы Unix epoch для проверки времени.
    /// \param nonce_store Подготовленный `NonceStore` или `nullptr`.
    /// \return `AntiReplayValidator` или `nullptr`.
    static std::unique_ptr<dfh_node::AntiReplayValidator>
    make_anti_replay_validator(const dfh_node::config::Config &cfg, EpochSystemClock &clock,
                               dfh_node::NonceStore *nonce_store) {
        if (!cfg.security.anti_replay.enabled || nonce_store == nullptr) {
            return nullptr;
        }

        return std::make_unique<dfh_node::AntiReplayValidator>(cfg.security.anti_replay, clock, *nonce_store);
    }

    /// \brief Останавливает transport и worker runtime без исключений.
    void shutdown() {
        if (m_ws_server != nullptr) {
            m_ws_server->shutdown();
            m_ws_server.reset();
            m_ws_router.reset();
        }

        m_http_server.shutdown();
        m_pool.shutdown();
    }

    const dfh_node::config::Config &m_cfg;
    dfh_node::TaskScheduler m_scheduler;
    dfh_node::WorkerPool m_pool;
    dfh_node::ConfigApiKeyStore m_config_key_store;
    dfh_node::FingerprintComputer m_fingerprint_computer;
    dfh_node::AuthCache m_auth_cache;
    dfh_node::MdbxApiKeyStore m_mdbx_store;
    dfh_node::CompositeApiKeyStore m_composite_key_store;
    dfh_node::DiskMonitor m_disk_monitor;
    dfh_node::ApiKeyManager m_key_manager;
    dfh_node::AuthService m_auth_service;
    dfh_node::RateLimiter m_rate_limiter;
    dfh_node::WsConnectionLimiter m_ws_connection_limiter;
    EpochSystemClock m_epoch_clock;
    std::unique_ptr<dfh_node::NonceStore> m_nonce_store;
    std::unique_ptr<dfh_node::AntiReplayValidator> m_anti_replay_validator;
    dfh_node::UnifiedGate m_gate;
    dfh_node::FakeDfhAdapter m_adapter;
    std::shared_ptr<dfh_node::transport::WsSessionRegistry> m_ws_registry;
    dfh_node::transport::HttpRouter m_http_router;
    dfh_node::transport::HttpServer m_http_server;
    dfh_node::transport::AdminRouter m_admin_router;
    dfh_node::transport::OpsRouter m_ops_router;
    std::unique_ptr<dfh_node::transport::WsRouter> m_ws_router;
    std::unique_ptr<dfh_node::transport::WsServer> m_ws_server;
};

} // namespace

DfhNodeApplication::DfhNodeApplication(int argc, char **argv) : m_argc(argc), m_argv(argv) {}

int DfhNodeApplication::run() {
    const CliParseResult cli = parse_cli_options(m_argc, m_argv);
    if (!cli.ok) {
        std::ostream &stream = cli.write_to_stderr ? std::cerr : std::cout;
        stream << cli.message;
        if (!cli.message.empty() && cli.message.back() != '\n') {
            stream << '\n';
        }
        return cli.exit_code;
    }

    const auto load_result = dfh_node::config::load_from_file(cli.options.config_path);
    if (!load_result.is_ok()) {
        print_errors(load_result.errors);
        return 1;
    }

    const auto validation_errors = dfh_node::config::validate(*load_result.config);
    if (!validation_errors.empty()) {
        print_errors(validation_errors);
        return 1;
    }

    const auto &cfg = *load_result.config;
    dfh_node::logging::init_logging(cfg.logging);

    try {
        RuntimeContext runtime(cfg);
        runtime.start_workers();
        dfh_node::logging::log_startup_status(cfg, build_startup_status(cfg, runtime.scheduler(), runtime.pool()));

        if (cli.options.run_mode) {
            runtime.start_transport_runtime();
            runtime.wait_for_stop_signal();
        }

        return 0;
    } catch (const std::exception &ex) {
        dfh_node::logging::log_runtime_bootstrap_error(ex.what());
        return 1;
    } catch (...) {
        dfh_node::logging::log_runtime_bootstrap_error("Failed to bootstrap node runtime: unknown error");
        return 1;
    }
}

} // namespace dfh_node::app
