/// \file mdbx_key_record.hpp
/// \brief Структура записи API-ключа для хранения в MDBX.
/// \details Описывает сериализуемую msgpack-модель, независимую от transport.

#pragma once

#include "auth/scope.hpp"

#include <msgpack.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace dfh_node {

/// \brief Запись API-ключа для хранения в MDBX.
struct MdbxKeyRecord {
    std::string id;                            ///< UUID v4 ключа.
    std::string name;                          ///< Уникальное имя ключа.
    std::string fingerprint;                   ///< HMAC-SHA256(server_secret, token) в hex.
    ScopeMask scope_mask = 0;                  ///< Битовая маска разрешённых scope.
    std::int64_t rps_limit = 0;                ///< `0` означает наследование `auth.rps_limit`.
    std::int64_t ws_max_connections = 0;       ///< `0` означает наследование `auth.ws_max_connections`.
    std::optional<std::int64_t> expires_at_ms; ///< Unix epoch ms; `std::nullopt` означает бессрочно.
    bool revoked = false;                      ///< Признак отзыва ключа.
    std::int64_t created_at_ms = 0;            ///< Unix epoch ms создания.
    std::int64_t updated_at_ms = 0;            ///< Unix epoch ms последнего обновления.

    MSGPACK_DEFINE_MAP(id, name, fingerprint, scope_mask, rps_limit, ws_max_connections, expires_at_ms, revoked,
                       created_at_ms, updated_at_ms);
};

} // namespace dfh_node
