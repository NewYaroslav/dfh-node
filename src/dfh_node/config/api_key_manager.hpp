/// \file api_key_manager.hpp
/// \brief Управление динамическими API-ключами в MDBX.
/// \details Инкапсулирует генерацию токенов, UUID и все мутации ключей с
/// немедленной инвалидацией `AuthCache`.

#pragma once

#include "auth/auth_cache.hpp"
#include "config/config.hpp"
#include "mdbx_api_key_store.hpp"
#include "security/fingerprint_computer.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dfh_node {

/// \brief Результат создания нового API-ключа.
struct CreateKeyResult {
    std::string id;       ///< UUID созданного ключа.
    std::string token;    ///< Plaintext токен; показывается один раз вызывающему коду.
    MdbxKeyRecord record; ///< Сохранённая запись без plaintext токена.
};

/// \brief Частичное обновление параметров API-ключа.
struct UpdateKeyRequest {
    std::optional<std::string> name;                          ///< Новое уникальное имя ключа.
    std::optional<ScopeMask> scope_mask;                      ///< Новая битовая маска scope.
    std::optional<std::int64_t> rps_limit;                    ///< Новый RPS-лимит; `0` означает наследование.
    std::optional<std::int64_t> ws_max_connections;           ///< Новый WS-лимит; `0` означает наследование.
    std::optional<std::optional<std::int64_t>> expires_at_ms; ///< Обновление срока действия.
};

/// \brief Сервис мутаций для MDBX API-ключей.
class ApiKeyManager {
public:
    /// \brief Создаёт менеджер для работы с динамическими ключами.
    /// \param store MDBX-хранилище динамических ключей.
    /// \param cache Auth-кэш для немедленной инвалидации после мутаций.
    /// \param fp_computer Вычислитель fingerprint для plaintext токенов.
    /// \param auth_cfg Конфигурация auth с дефолтными лимитами.
    ApiKeyManager(MdbxApiKeyStore &store, AuthCache &cache, const FingerprintComputer &fp_computer,
                  const config::AuthConfig &auth_cfg);

    /// \brief Создаёт новый API-ключ.
    /// \param name Уникальное имя ключа.
    /// \param scope_mask Битовая маска scope.
    /// \param rps_limit Индивидуальный RPS-лимит; `0` означает наследование.
    /// \param ws_max_connections Индивидуальный WS-лимит; `0` означает наследование.
    /// \param expires_at_ms Срок действия; `std::nullopt` означает бессрочно.
    /// \return UUID, plaintext токен и сохранённая запись.
    /// \throws std::runtime_error при дубликате имени или ошибке генерации случайных данных.
    CreateKeyResult create(const std::string &name, ScopeMask scope_mask, std::int64_t rps_limit,
                           std::int64_t ws_max_connections, std::optional<std::int64_t> expires_at_ms);

    /// \brief Возвращает ключ по UUID.
    /// \param id UUID ключа.
    /// \return Полная запись из MDBX или `std::nullopt`.
    std::optional<MdbxKeyRecord> get_by_id(const std::string &id) const;

    /// \brief Возвращает список ключей.
    /// \param include_revoked Включать ли revoked-записи.
    /// \return Список ключей из MDBX.
    std::vector<MdbxKeyRecord> list(bool include_revoked) const;

    /// \brief Обновляет существующий ключ.
    /// \param id UUID ключа.
    /// \param req Частичный набор изменений.
    /// \return `true`, если ключ найден и обновлён.
    bool update(const std::string &id, const UpdateKeyRequest &req);

    /// \brief Отзывает ключ идемпотентно.
    /// \param id UUID ключа.
    /// \param already_revoked Возвращает признак повторного revoke.
    /// \return `true`, если ключ найден.
    bool revoke(const std::string &id, bool &already_revoked);

    /// \brief Физически удаляет ключ.
    /// \param id UUID ключа.
    /// \return `true`, если ключ найден и удалён.
    bool remove(const std::string &id);

private:
    /// \brief Генерирует plaintext токен из 32 случайных байт.
    /// \return Lowercase hex строка длиной 64 символа.
    std::string generate_token() const;

    /// \brief Генерирует UUID v4 из 16 случайных байт.
    /// \return UUID в canonical lowercase text form.
    std::string generate_uuid() const;

    /// \brief Возвращает текущее Unix-время в миллисекундах.
    /// \return Unix epoch ms.
    static std::int64_t now_epoch_ms();

    MdbxApiKeyStore &m_store;
    AuthCache &m_cache;
    const FingerprintComputer &m_fp;
    const config::AuthConfig &m_auth_cfg;
};

} // namespace dfh_node
