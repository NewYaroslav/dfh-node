/// \file ops_router.cpp
/// \brief Реализация роутера эксплуатационных HTTP-endpoint'ов.
/// \details Содержит лёгкие обработчики health/readiness и защищённую выдачу
/// Prometheus-метрик.
///
#include "ops_router.hpp"

#include "http_error_map.hpp"
#include "transport/transport_security_utils.hpp"

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>

namespace dfh_node::transport {
namespace {

void send_response(const OpsRouter::HttpResponse &response, const int status, const std::string &body,
                   const std::string &content_type, SimpleWeb::CaseInsensitiveMultimap extra_headers = {}) {
    extra_headers.emplace("Content-Type", content_type);
    response->write(static_cast<SimpleWeb::StatusCode>(status), body, extra_headers);
    response->send();
}

void send_json(const OpsRouter::HttpResponse &response, const int status, const nlohmann::json &body) {
    send_response(response, status, body.dump(), "application/json");
}

void send_gate_error(const OpsRouter::HttpResponse &response, const GateError &error) {
    const auto [status, body] = gate_error_to_http(error);
    send_response(response, status, body, "application/json");
}

std::string build_prometheus_metrics(const QueueMetrics &high_metrics, const QueueMetrics &low_metrics,
                                     const WorkerPool &worker_pool, const std::uint64_t disk_free_bytes,
                                     const bool disk_low) {
    std::ostringstream stream;
    stream << "# HELP dfh_node_queue_size Current queue size\n"
           << "# TYPE dfh_node_queue_size gauge\n"
           << "dfh_node_queue_size{lane=\"high\"} " << high_metrics.current_size << '\n'
           << "dfh_node_queue_size{lane=\"low\"} " << low_metrics.current_size << '\n'
           << "# HELP dfh_node_total_processed Total processed tasks\n"
           << "# TYPE dfh_node_total_processed counter\n"
           << "dfh_node_total_processed{lane=\"high\"} " << worker_pool.total_processed(TaskLane::High) << '\n'
           << "dfh_node_total_processed{lane=\"low\"} " << worker_pool.total_processed(TaskLane::Low) << '\n'
           << "# HELP dfh_node_avg_wait_ms Average wait time ms\n"
           << "# TYPE dfh_node_avg_wait_ms gauge\n"
           << "dfh_node_avg_wait_ms{lane=\"high\"} " << worker_pool.avg_wait_ms(TaskLane::High) << '\n'
           << "dfh_node_avg_wait_ms{lane=\"low\"} " << worker_pool.avg_wait_ms(TaskLane::Low) << '\n'
           << "# HELP dfh_node_disk_free_bytes Free disk space bytes\n"
           << "# TYPE dfh_node_disk_free_bytes gauge\n"
           << "dfh_node_disk_free_bytes " << disk_free_bytes << '\n'
           << "# HELP dfh_node_disk_low Disk low flag\n"
           << "# TYPE dfh_node_disk_low gauge\n"
           << "dfh_node_disk_low " << (disk_low ? 1 : 0) << '\n';
    return stream.str();
}

} // namespace

OpsRouter::OpsRouter(UnifiedGate &gate, DiskMonitor &disk_monitor, MdbxApiKeyStore &mdbx_store,
                     TaskScheduler &scheduler, WorkerPool &worker_pool, const config::Config &cfg)
    : m_gate(gate), m_disk_monitor(disk_monitor), m_mdbx_store(mdbx_store), m_scheduler(scheduler),
      m_worker_pool(worker_pool), m_cfg(cfg) {}

void OpsRouter::register_all(SimpleWeb::Server<SimpleWeb::HTTP> &server) {
    (void)m_cfg;

    server.resource["^/health$"]["GET"] = [this](const HttpResponse &response, const HttpRequest &request) {
        handle_health(request, response);
    };
    server.resource["^/ready$"]["GET"] = [this](const HttpResponse &response, const HttpRequest &request) {
        handle_ready(request, response);
    };
    server.resource["^/metrics$"]["GET"] = [this](const HttpResponse &response, const HttpRequest &request) {
        handle_metrics(request, response);
    };
}

void OpsRouter::handle_health(HttpRequest req, HttpResponse resp) {
    (void)req;

    nlohmann::json body;
    body["status"] = "ok";
    send_json(resp, 200, body);
}

void OpsRouter::handle_ready(HttpRequest req, HttpResponse resp) {
    (void)req;

    const bool mdbx_healthy = m_mdbx_store.is_healthy();
    const bool disk_ok = !m_disk_monitor.is_disk_low();
    const bool workers_running = m_worker_pool.is_running();

    nlohmann::json body;
    if (mdbx_healthy && disk_ok && workers_running) {
        body["ready"] = true;
        send_json(resp, 200, body);
        return;
    }

    body["ready"] = false;
    body["mdbx_healthy"] = mdbx_healthy;
    body["disk_ok"] = disk_ok;
    body["workers_running"] = workers_running;
    send_json(resp, 503, body);
}

void OpsRouter::handle_metrics(HttpRequest req, HttpResponse resp) {
    const std::string token = extract_bearer_token(req->header);
    const GateResult gate_result = m_gate.authorize_http(token, TaskKind::Admin, nullptr);
    if (const auto *gate_error = std::get_if<GateError>(&gate_result)) {
        send_gate_error(resp, *gate_error);
        return;
    }

    const QueueMetrics high_metrics = m_scheduler.high_metrics();
    const QueueMetrics low_metrics = m_scheduler.low_metrics();
    const bool disk_low = m_disk_monitor.is_disk_low();
    const std::uint64_t disk_free_bytes = m_disk_monitor.last_free_bytes();

    send_response(resp, 200,
                  build_prometheus_metrics(high_metrics, low_metrics, m_worker_pool, disk_free_bytes, disk_low),
                  "text/plain; version=0.0.4");
}

} // namespace dfh_node::transport
