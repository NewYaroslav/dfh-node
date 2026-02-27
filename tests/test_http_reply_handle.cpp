/// \file test_http_reply_handle.cpp
/// \brief Интеграционные тесты `HttpReplyHandle` через локальный HTTP-сервер.
/// \details Проверяет гонку timer/worker, однократную отправку и сценарий с ранним разрывом клиента.
///
#include "test_helpers.hpp"
#include "transport/http/http_reply_handle.hpp"

#include <client_http.hpp>
#include <nlohmann/json.hpp>
#include <server_http.hpp>

#ifdef USE_STANDALONE_ASIO
#include <asio.hpp>
#include <asio/ip/tcp.hpp>
#else
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
namespace asio = boost::asio;
#endif

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>

namespace {

using SwsServer = SimpleWeb::Server<SimpleWeb::HTTP>;
using SwsClient = SimpleWeb::Client<SimpleWeb::HTTP>;
using SwsHeaders = SimpleWeb::CaseInsensitiveMultimap;

struct HttpResponse {
    int status{0};
    std::string body;
    SwsHeaders headers;
};

int parse_status_code(const std::string &value) {
    CHECK(value.size() >= 3);
    return std::stoi(value.substr(0, 3));
}

unsigned short acquire_free_port() {
    asio::io_service io_service;
    asio::ip::tcp::acceptor acceptor(io_service);
    acceptor.open(asio::ip::tcp::v4());
    acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    const unsigned short port = acceptor.local_endpoint().port();
    acceptor.close();
    return port;
}

HttpResponse request_http(const unsigned short port, const std::string &path, long timeout_seconds = 5) {
    SwsClient client("127.0.0.1:" + std::to_string(port));
    client.config.timeout = timeout_seconds;
    auto response = client.request("GET", path);

    HttpResponse result;
    result.status = parse_status_code(response->status_code);
    result.body = response->content.string();
    result.headers = response->header;
    return result;
}

void wait_until_ready(const unsigned short port) {
    for (int attempt = 0; attempt < 80; ++attempt) {
        try {
            const auto response = request_http(port, "/ready", 1);
            if (response.status == 200) {
                return;
            }
        } catch (...) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    CHECK(false);
}

class ReplyHandleTestServer {
public:
    ReplyHandleTestServer() : m_port(acquire_free_port()) {}

    void start() {
        m_server.config.address = "127.0.0.1";
        m_server.config.port = m_port;
        m_server.io_service = std::make_shared<asio::io_service>();

        m_server.resource["^/ready$"]["GET"] = [](const std::shared_ptr<SwsServer::Response> &response,
                                                  const std::shared_ptr<SwsServer::Request> &request) {
            (void)request;
            response->write(SimpleWeb::StatusCode::success_ok, "ready");
            response->send();
        };

        m_server.resource["^/ok_from_worker$"]["GET"] = [this](const std::shared_ptr<SwsServer::Response> &response,
                                                               const std::shared_ptr<SwsServer::Request> &request) {
            (void)request;
            dfh_node::transport::HttpReplyHandle handle(response, m_server.io_service, "ok_from_worker");
            std::thread([handle]() mutable { handle.reply_ok("ok", "text/plain"); }).detach();
        };

        m_server.resource["^/timeout_first$"]["GET"] = [this](const std::shared_ptr<SwsServer::Response> &response,
                                                              const std::shared_ptr<SwsServer::Request> &request) {
            (void)request;
            dfh_node::transport::HttpReplyHandle handle(response, m_server.io_service, "timeout_first");
            handle.start_timeout(std::chrono::milliseconds(20));
            std::thread([handle]() mutable {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                handle.reply_ok("late", "text/plain");
            }).detach();
        };

        m_server.resource["^/ok_before_timeout$"]["GET"] = [this](const std::shared_ptr<SwsServer::Response> &response,
                                                                  const std::shared_ptr<SwsServer::Request> &request) {
            (void)request;
            dfh_node::transport::HttpReplyHandle handle(response, m_server.io_service, "ok_before_timeout");
            handle.start_timeout(std::chrono::milliseconds(200));
            handle.reply_ok("fast", "text/plain");
        };

        m_server.resource["^/double_ok$"]["GET"] = [this](const std::shared_ptr<SwsServer::Response> &response,
                                                          const std::shared_ptr<SwsServer::Request> &request) {
            (void)request;
            dfh_node::transport::HttpReplyHandle handle(response, m_server.io_service, "double_ok");
            handle.reply_ok("first", "text/plain");
            handle.reply_ok("second", "text/plain");
        };

        m_server.resource["^/timeout_disabled$"]["GET"] = [this](const std::shared_ptr<SwsServer::Response> &response,
                                                                 const std::shared_ptr<SwsServer::Request> &request) {
            (void)request;
            dfh_node::transport::HttpReplyHandle handle(response, m_server.io_service, "timeout_disabled");
            handle.start_timeout(std::chrono::milliseconds(0));
            std::thread([handle]() mutable {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                handle.reply_ok("ok0", "text/plain");
            }).detach();
        };

        m_server.resource["^/client_disconnect$"]["GET"] = [this](const std::shared_ptr<SwsServer::Response> &response,
                                                                  const std::shared_ptr<SwsServer::Request> &request) {
            (void)request;
            dfh_node::transport::HttpReplyHandle handle(response, m_server.io_service, "client_disconnect");
            handle.start_timeout(std::chrono::milliseconds(0));
            std::thread([handle]() mutable {
                std::this_thread::sleep_for(std::chrono::milliseconds(1200));
                handle.reply_ok("after_disconnect", "text/plain");
            }).detach();
        };

        m_server.default_resource["GET"] = [](const std::shared_ptr<SwsServer::Response> &response,
                                              const std::shared_ptr<SwsServer::Request> &request) {
            (void)request;
            response->write(SimpleWeb::StatusCode::client_error_not_found, "not found");
            response->send();
        };

        m_server.bind();
        m_thread = std::thread([this]() {
            m_server.accept_and_run();
            if (m_server.io_service->stopped()) {
                m_server.io_service->reset();
            }
            m_server.io_service->run();
        });

        wait_until_ready(m_port);
    }

    void stop() {
        m_server.stop();
        if (m_server.io_service) {
            m_server.io_service->stop();
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    unsigned short port() const { return m_port; }

private:
    unsigned short m_port{0};
    SwsServer m_server;
    std::thread m_thread;
};

void test_reply_ok_from_worker_thread(const ReplyHandleTestServer &server) {
    const auto response = request_http(server.port(), "/ok_from_worker");
    CHECK_EQ(response.status, 200);
    CHECK_EQ(response.body, "ok");
}

void test_timeout_wins_over_late_reply(const ReplyHandleTestServer &server) {
    const auto response = request_http(server.port(), "/timeout_first");
    CHECK_EQ(response.status, 504);

    const nlohmann::json parsed = nlohmann::json::parse(response.body);
    CHECK_EQ(parsed.at("error").get<std::string>(), "timeout");
}

void test_ok_wins_over_timer(const ReplyHandleTestServer &server) {
    const auto response = request_http(server.port(), "/ok_before_timeout");
    CHECK_EQ(response.status, 200);
    CHECK_EQ(response.body, "fast");

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

void test_double_reply_sends_only_once(const ReplyHandleTestServer &server) {
    const auto response = request_http(server.port(), "/double_ok");
    CHECK_EQ(response.status, 200);
    CHECK_EQ(response.body, "first");
}

void test_timeout_zero_disables_timer(const ReplyHandleTestServer &server) {
    const auto response = request_http(server.port(), "/timeout_disabled");
    CHECK_EQ(response.status, 200);
    CHECK_EQ(response.body, "ok0");
}

void test_client_disconnect_does_not_crash(const ReplyHandleTestServer &server) {
    SwsClient client("127.0.0.1:" + std::to_string(server.port()));
    client.config.timeout = 1;

    bool request_failed = false;
    try {
        (void)client.request("GET", "/client_disconnect");
    } catch (...) {
        request_failed = true;
    }

    CHECK(request_failed);
    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
}

} // namespace

int main() {
    ReplyHandleTestServer server;
    server.start();

    test_reply_ok_from_worker_thread(server);
    test_timeout_wins_over_late_reply(server);
    test_ok_wins_over_timer(server);
    test_double_reply_sends_only_once(server);
    test_timeout_zero_disables_timer(server);
    test_client_disconnect_does_not_crash(server);

    server.stop();
    return 0;
}
