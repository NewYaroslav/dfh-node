/// \file http_reply_handle.cpp
/// \brief Реализация отложенного ответа для HTTP transport.
/// \details Сериализует отправку через один `io_service`, чтобы избежать гонок записи в SWS `Response`.
///
#include "http_reply_handle.hpp"

#include "transport/http/http_error_map.hpp"

#include <iostream>
#include <utility>

namespace dfh_node::transport {

ReplyState::ReplyState(std::shared_ptr<SwsResponse> response_arg, std::shared_ptr<asio::io_service> executor_arg,
                       std::string request_id_arg)
    : response(std::move(response_arg)), executor(std::move(executor_arg)), timer(*executor),
      request_id(std::move(request_id_arg)) {}

HttpReplyHandle::HttpReplyHandle(std::shared_ptr<SwsResponse> response, std::shared_ptr<asio::io_service> executor,
                                 std::string request_id)
    : m_state(std::make_shared<ReplyState>(std::move(response), std::move(executor), std::move(request_id))) {}

void HttpReplyHandle::start_timeout(const std::chrono::milliseconds timeout) {
    if (timeout.count() == 0) {
        return;
    }

    auto state = m_state;
    HttpReplyHandle self = *this;
    state->timer.expires_from_now(timeout);
    state->timer.async_wait([state, self](const SimpleWeb::error_code &ec) mutable {
        if (ec) {
            return;
        }
        if (state->m_replied.exchange(true)) {
            return;
        }

        state->m_cancelled.store(true);
        self.do_send(504, make_error_body("timeout", "request timeout"), "application/json", {});
    });
}

void HttpReplyHandle::reply_ok(std::string body, std::string content_type) {
    reply_ok(std::move(body), std::move(content_type), {});
}

void HttpReplyHandle::reply_ok(std::string body, std::string content_type,
                               SimpleWeb::CaseInsensitiveMultimap extra_headers) {
    if (m_state->m_replied.exchange(true)) {
        if (m_state->m_cancelled.load()) {
            std::clog << "WARN: deferred reply discarded: timeout fired first, request_id=" << m_state->request_id
                      << '\n';
            return;
        }
        std::clog << "WARN: deferred reply discarded: already replied, request_id=" << m_state->request_id << '\n';
        return;
    }

    if (m_state->m_cancelled.load()) {
        std::clog << "WARN: deferred reply discarded: timeout fired first, request_id=" << m_state->request_id << '\n';
        return;
    }

    SimpleWeb::error_code cancel_ec;
    m_state->timer.cancel(cancel_ec);
    do_send(200, std::move(body), std::move(content_type), std::move(extra_headers));
}

void HttpReplyHandle::reply_error(const int http_status, const std::string_view error_code,
                                  const std::string_view detail) {
    if (m_state->m_replied.exchange(true)) {
        if (m_state->m_cancelled.load()) {
            std::clog << "WARN: deferred reply discarded: timeout fired first, request_id=" << m_state->request_id
                      << '\n';
            return;
        }
        std::clog << "WARN: deferred reply discarded: already replied, request_id=" << m_state->request_id << '\n';
        return;
    }

    if (m_state->m_cancelled.load()) {
        std::clog << "WARN: deferred reply discarded: timeout fired first, request_id=" << m_state->request_id << '\n';
        return;
    }

    SimpleWeb::error_code cancel_ec;
    m_state->timer.cancel(cancel_ec);
    do_send(http_status, make_error_body(error_code, detail), "application/json", {});
}

void HttpReplyHandle::do_send(const int status, std::string body, std::string content_type,
                              SimpleWeb::CaseInsensitiveMultimap extra_headers) {
    auto state = m_state;
    // Проверка сделана на submodule `simple-web-server` коммита 35ebb10782507f887802df64a2b6bfc8b427d81f:
    // `Response::send()` вызывает `asio::async_write` напрямую и не сериализует конкурентные вызовы через `strand`.
    // Поэтому все отправки планируем через один `io_service` методом `post`.
    state->executor->post([state, status, body = std::move(body), content_type = std::move(content_type),
                           extra_headers = std::move(extra_headers)]() mutable {
        try {
            SimpleWeb::CaseInsensitiveMultimap headers = std::move(extra_headers);
            headers.emplace("Content-Type", content_type);
            state->response->write(static_cast<SimpleWeb::StatusCode>(status), body, headers);
            state->response->send();
        } catch (const std::exception &e) {
            std::clog << "WARN: deferred send failed: " << e.what() << ", request_id=" << state->request_id << '\n';
        } catch (...) {
            std::clog << "WARN: deferred send failed: unknown error, request_id=" << state->request_id << '\n';
        }
    });
}

} // namespace dfh_node::transport
