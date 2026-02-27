/// \file auth.hpp
/// \brief Umbrella-заголовок auth-компонентов.
/// \details Подключает scope, auth cache/service, rate limiter и unified gate.
///
#pragma once

#include "auth/scope.hpp"
#include "auth/auth_cache.hpp"
#include "auth/auth_service.hpp"
#include "auth/rate_limiter.hpp"
#include "auth/ws_connection_limiter.hpp"
#include "auth/unified_gate.hpp"
