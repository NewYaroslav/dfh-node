/// \file http_router.cpp
/// \brief Реализация регистрации HTTP-маршрутов transport-слоя.
/// \details На текущем этапе маршруты регистрируются как заглушки до полной бизнес-логики.
///
#include "http_router.hpp"

#include "http_error_map.hpp"

namespace dfh_node::transport {
namespace {

using SwsResponse = SimpleWeb::Server<SimpleWeb::HTTP>::Response;

/// Отправляет единообразный ответ для заглушек роутера.
void send_not_implemented(const std::shared_ptr<SwsResponse> &response, const std::string_view route_name) {
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    response->write(
        SimpleWeb::StatusCode::server_error_not_implemented,
        make_error_body("not_implemented", std::string("route is not implemented yet: ") + std::string(route_name)),
        headers);
    response->send();
}

} // namespace

HttpRouter::HttpRouter(UnifiedGate &gate, TaskScheduler &scheduler, IDfhAdapter &adapter, const config::HttpConfig &cfg)
    : m_gate(gate), m_scheduler(scheduler), m_adapter(adapter), m_cfg(cfg) {}

void HttpRouter::register_all(SimpleWeb::Server<SimpleWeb::HTTP> &server) {
    m_executor = server.io_service;

    server.resource["^/v1/ingest$"]["POST"] =
        [](const std::shared_ptr<SwsResponse> &response,
           const std::shared_ptr<SimpleWeb::Server<SimpleWeb::HTTP>::Request> &request) {
            (void)request;
            send_not_implemented(response, "/v1/ingest");
        };

    server.resource["^/v1/history$"]["GET"] =
        [](const std::shared_ptr<SwsResponse> &response,
           const std::shared_ptr<SimpleWeb::Server<SimpleWeb::HTTP>::Request> &request) {
            (void)request;
            send_not_implemented(response, "/v1/history");
        };

    server.resource["^/v1/status$"]["GET"] =
        [](const std::shared_ptr<SwsResponse> &response,
           const std::shared_ptr<SimpleWeb::Server<SimpleWeb::HTTP>::Request> &request) {
            (void)request;
            send_not_implemented(response, "/v1/status");
        };
}

} // namespace dfh_node::transport
