/// \file http_reply_handle.hpp
/// \brief Дескриптор отложенного HTTP-ответа для рабочих потоков.
/// \details Обеспечивает однократную отправку ответа и таймаут через `asio::steady_timer`.
///
#pragma once

#include <server_http.hpp>

#ifdef USE_STANDALONE_ASIO
#include <asio.hpp>
#include <asio/steady_timer.hpp>
#else
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
namespace asio = boost::asio;
#endif

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace dfh_node::transport {

/// \brief Внутреннее состояние отложенного ответа.
struct ReplyState {
    using SwsResponse = SimpleWeb::Server<SimpleWeb::HTTP>::Response;

    /// \brief Конструктор состояния отложенного ответа.
    /// \param response Сохранённый объект ответа SWS.
    /// \param executor `io_service`, в котором выполняется отправка и таймер.
    /// \param request_id Идентификатор запроса для логов.
    ReplyState(std::shared_ptr<SwsResponse> response, std::shared_ptr<asio::io_service> executor,
               std::string request_id);

    std::shared_ptr<SwsResponse> response;      ///< Ответ SWS, сохраняемый между потоками.
    std::shared_ptr<asio::io_service> executor; ///< Executor от `HttpServer`.
    asio::steady_timer timer;                   ///< Таймер на том же `io_service`.
    std::atomic<bool> m_replied{false};         ///< Replier token: первый отправивший выставляет `true`.
    std::atomic<bool> m_cancelled{false};       ///< Флаг сработавшего таймаута.
    std::string request_id;                     ///< Идентификатор запроса для логирования.
};

/// \brief Обёртка отложенного ответа для worker-потока.
/// \details Handler SWS немедленно возвращается, а worker вызывает `reply_ok`/`reply_error`.
/// Только один из `{таймер, worker}` выполнит фактическую отправку.
class HttpReplyHandle {
public:
    using SwsResponse = ReplyState::SwsResponse;

    /// \brief Конструктор.
    /// \param response SWS Response, сохранённый в handler.
    /// \param executor Executor из `HttpServer` для `post`.
    /// \param request_id Идентификатор запроса для логирования.
    HttpReplyHandle(std::shared_ptr<SwsResponse> response, std::shared_ptr<asio::io_service> executor,
                    std::string request_id);

    /// \brief Запускает таймаут ответа.
    /// \param timeout Таймаут отложенного ответа; `0ms` отключает таймер.
    void start_timeout(std::chrono::milliseconds timeout);

    /// \brief Отправляет успешный ответ (HTTP 200).
    /// \param body Тело ответа.
    /// \param content_type MIME-тип тела.
    void reply_ok(std::string body, std::string content_type);

    /// \brief Отправляет ошибку.
    /// \param http_status HTTP-статус (400, 401, 503, ...).
    /// \param error_code Стабильный код ошибки для поля `error`.
    /// \param detail Дополнительная диагностическая строка для поля `detail`.
    void reply_error(int http_status, std::string_view error_code, std::string_view detail = "");

private:
    /// \brief Единственное место вызова `response->send()`.
    /// \details Отправка всегда планируется в `executor` через `post`.
    void do_send(int status, std::string body, std::string content_type);

    std::shared_ptr<ReplyState> m_state; ///< Разделяемое состояние для безопасного копирования в `Task`.
};

} // namespace dfh_node::transport
