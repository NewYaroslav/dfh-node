/// \file transport.hpp
/// \brief Umbrella-заголовок transport-слоя.
/// \details Подключает HTTP/WS transport API и утилиты протоколов.
///
#pragma once

// clang-format off
// Важно: сначала WS-заголовки, иначе include guard `SIMPLE_WEB_UTILITY_HPP`
// из HTTP-ветки скрывает определения `string_view`/`DEPRECATED` для `server_ws.hpp`.
#include "transport/ws/ws_protocol.hpp"
#include "transport/ws/ws_router.hpp"
#include "transport/ws/ws_server.hpp"
#include "transport/ws/ws_session_registry.hpp"
#include "transport/http/admin_dto_parser.hpp"
#include "transport/http/admin_router.hpp"
#include "transport/http/http_error_map.hpp"
#include "transport/http/ops_router.hpp"
#include "transport/http/http_reply_handle.hpp"
#include "transport/http/http_router.hpp"
#include "transport/http/http_server.hpp"
// clang-format on
