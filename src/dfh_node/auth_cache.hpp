/**
 * \file auth_cache.hpp
 * \brief Кэш контекстов авторизации с TTL и периодической очисткой.
 * \details Предназначен для ускорения проверок fingerprint без повторного
 * чтения store.
 */
#pragma once

#include "scope.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace dfh_node {

/// \brief Контекст авторизованного клиента.
struct AuthContext {
    std::string fingerprint;    ///< Fingerprint токена (hex HMAC-SHA256).
    ScopeMask scope_mask = 0;   ///< Разрешённые права доступа.
    std::int64_t rps_limit = 0; ///< Лимит запросов в секунду.
    std::int64_t ws_max_connections = 0; ///< Лимит одновременных WS-соединений.
    std::optional<std::int64_t>
        expires_at_ms; ///< Время истечения токена (epoch ms).
};

/// \brief Запись кэша для AuthContext.
struct CachedAuthContext {
    AuthContext context;           ///< Кэшируемый контекст.
    std::int64_t cached_at_ms = 0; ///< Время помещения в кэш (steady_clock ms).
    std::int64_t updated_at_ms =
        0; ///< Резерв под future invalidation/revision.
};

/// \brief Потокобезопасный auth-кэш с TTL и opportunistic cleanup.
class AuthCache {
  public:
    /// \brief Создаёт кэш с заданным TTL.
    /// \param ttl_ms Время жизни записи в миллисекундах по steady_clock.
    explicit AuthCache(std::int64_t ttl_ms);

    /// \brief Читает контекст по fingerprint с проверкой TTL.
    /// \param fingerprint Ключ кэша.
    /// \return Контекст при наличии и неистёкшем TTL, иначе std::nullopt.
    std::optional<AuthContext> get(const std::string &fingerprint) const;

    /// \brief Обновляет/добавляет контекст в кэш.
    /// \param fingerprint Ключ кэша.
    /// \param context Контекст авторизации.
    void put(const std::string &fingerprint, const AuthContext &context);

    /// \brief Удаляет запись по fingerprint.
    /// \param fingerprint Ключ кэша.
    void invalidate(const std::string &fingerprint);

    /// \brief Полностью очищает кэш.
    void clear();

  private:
    std::int64_t m_ttl_ms;
    std::unordered_map<std::string, CachedAuthContext> m_cache;
    mutable std::shared_mutex m_mutex;
    std::atomic<std::uint64_t> m_operation_count{0};
    static constexpr std::uint64_t k_cleanup_interval = 1000;

    std::int64_t steady_clock_ms() const;
    void cleanup_expired_locked();
};

} // namespace dfh_node
