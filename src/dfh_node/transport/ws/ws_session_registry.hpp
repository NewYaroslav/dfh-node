/// \file ws_session_registry.hpp
/// \brief Потокобезопасный реестр WS-соединений и их контекста.
/// \details Хранит weak-ссылки на соединения, контекст авторизации и состояние
/// ожидаемого `dfhbin` binary frame для каждого `connection_id`.
///
#pragma once

#include "adapter/dfh_adapter_dto.hpp"
#include "auth/scope.hpp"

#include <server_ws.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace dfh_node::transport {

/// \brief Копируемый контекст WS-соединения.
/// \details Содержит данные, полученные на этапе upgrade/handshake.
/// Поле `signing_key` очищается в деструкторе через `OPENSSL_cleanse`.
struct WsConnectionContext {
    std::string connection_id;                          ///< Идентификатор соединения (`ws-<counter>`).
    std::string fingerprint;                            ///< Fingerprint ключа (`HMAC(server_secret, token)`).
    std::uint8_t signing_key[32]{};                     ///< Ключ подписи `SHA256(token)` (сырые 32 байта).
    ScopeMask scope_mask = 0;                           ///< Битовая маска scope.
    std::string endpoint;                               ///< Endpoint соединения (`/ws/json` или `/ws/msgpack`).
    std::chrono::steady_clock::time_point connected_at; ///< Время установки соединения.

    /// \brief Очищает `signing_key` перед уничтожением контекста.
    ~WsConnectionContext();
};

/// \brief Состояние ожидаемого `dfhbin` binary frame.
/// \details Тип сделан move-only, чтобы исключить неявное копирование payload-метаданных.
struct PendingDfhbinState {
    std::string msg_id;         ///< `msg_id` control-message.
    std::string payload_sha256; ///< Ожидаемый SHA-256 binary frame в hex.
    BlockKey block_key;         ///< Ключ блока для ingest `dfhbin`.

    PendingDfhbinState() = default;
    PendingDfhbinState(const PendingDfhbinState &) = delete;
    PendingDfhbinState &operator=(const PendingDfhbinState &) = delete;
    PendingDfhbinState(PendingDfhbinState &&) noexcept = default;
    PendingDfhbinState &operator=(PendingDfhbinState &&) noexcept = default;
};

/// \brief Реестр WS-соединений.
/// \details Обеспечивает:
/// - доступ по `connection_id`;
/// - обратный O(1) lookup `Connection* -> connection_id`;
/// - хранение pending-состояния для двушагового протокола `dfhbin`.
class WsSessionRegistry {
public:
    using ConnectionId = std::string;
    using SwsConnection = SimpleWeb::SocketServer<SimpleWeb::WS>::Connection;

    /// \brief Зарегистрировать новое соединение.
    /// \param conn Указатель на SWS-соединение.
    /// \param ctx Контекст соединения.
    /// \return Сгенерированный `connection_id`.
    ConnectionId register_connection(std::shared_ptr<SwsConnection> conn, WsConnectionContext ctx);

    /// \brief Удалить соединение из реестра.
    /// \param id Идентификатор соединения.
    void unregister_connection(const ConnectionId &id);

    /// \brief Найти `connection_id` по raw-указателю соединения.
    /// \param raw Raw-указатель SWS-соединения из callback.
    /// \return `connection_id` при наличии, иначе `std::nullopt`.
    std::optional<ConnectionId> find_id(const SwsConnection *raw) const;

    /// \brief Получить weak-ссылку на соединение.
    /// \param id Идентификатор соединения.
    /// \return Weak-ссылка или пустая weak-ссылка, если соединение не найдено.
    std::weak_ptr<SwsConnection> get_connection(const ConnectionId &id) const;

    /// \brief Получить копию контекста соединения.
    /// \param id Идентификатор соединения.
    /// \return Копия `WsConnectionContext` либо `std::nullopt`.
    std::optional<WsConnectionContext> get_context(const ConnectionId &id) const;

    /// \brief Сохранить pending-состояние `dfhbin`.
    /// \param id Идентификатор соединения.
    /// \param state Состояние, ожидающее следующий binary frame.
    void set_pending_dfhbin(const ConnectionId &id, PendingDfhbinState state);

    /// \brief Очистить pending-состояние `dfhbin`.
    /// \param id Идентификатор соединения.
    void clear_pending_dfhbin(const ConnectionId &id);

    /// \brief Извлечь pending-состояние `dfhbin` с очисткой.
    /// \param id Идентификатор соединения.
    /// \return Состояние (move) либо `std::nullopt`.
    std::optional<PendingDfhbinState> take_pending_dfhbin(const ConnectionId &id);

    /// \brief Текущее число зарегистрированных соединений.
    /// \return Размер реестра.
    std::size_t size() const;

private:
    struct Entry {
        std::weak_ptr<SwsConnection> conn_weak;    ///< Ссылка на соединение без владения.
        WsConnectionContext ctx;                   ///< Контекст соединения.
        std::optional<PendingDfhbinState> pending; ///< Pending-состояние `dfhbin`.
        bool closed{false};                        ///< Флаг закрытия (читается/пишется только под `m_mutex`).
    };

    mutable std::mutex m_mutex;
    std::unordered_map<ConnectionId, Entry> m_entries;
    std::unordered_map<const SwsConnection *, ConnectionId> m_ptr_to_id;
    std::atomic<std::uint64_t> m_counter{0};
};

} // namespace dfh_node::transport
