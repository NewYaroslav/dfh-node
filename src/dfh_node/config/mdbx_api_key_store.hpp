/// \file mdbx_api_key_store.hpp
/// \brief MDBX-реализация хранилища API-ключей.
/// \details Хранит первичный индекс по `id` и secondary-индексы по `fingerprint` и `name`.

#pragma once

#include "api_key_store.hpp"
#include "mdbx_key_record.hpp"

#include <mdbx.h++>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace dfh_node {

/// \brief Хранилище API-ключей на базе MDBX.
class MdbxApiKeyStore final : public IApiKeyStore {
public:
    /// \brief Создаёт store с путём к MDBX environment.
    /// \param env_path Путь к файлу MDBX environment.
    explicit MdbxApiKeyStore(std::string env_path);

    /// \brief Открывает MDBX environment и создаёт требуемые таблицы.
    void open();

    /// \brief Ищет активный ключ по fingerprint.
    /// \param fingerprint HMAC-SHA256(server_secret, token) в hex.
    /// \return Активная запись ключа, иначе `std::nullopt`.
    std::optional<ApiKeyRecord> lookup(const std::string &fingerprint) const override;

    /// \brief Вставляет или обновляет запись ключа.
    /// \param rec Сериализуемая запись ключа.
    /// \throws std::runtime_error если нарушена уникальность `name` или `fingerprint`.
    void put(const MdbxKeyRecord &rec);

    /// \brief Читает запись по UUID.
    /// \param id UUID ключа.
    /// \return Полная запись из MDBX или `std::nullopt`.
    std::optional<MdbxKeyRecord> get_by_id(const std::string &id) const;

    /// \brief Читает запись по уникальному имени.
    /// \param name Уникальное имя ключа.
    /// \return Полная запись из MDBX или `std::nullopt`.
    std::optional<MdbxKeyRecord> get_by_name(const std::string &name) const;

    /// \brief Физически удаляет запись из всех индексов.
    /// \param id UUID ключа.
    /// \return `true`, если запись существовала и была удалена.
    bool remove(const std::string &id);

    /// \brief Возвращает все записи, включая revoked.
    /// \return Список всех записей в порядке обхода `keys_by_id`.
    std::vector<MdbxKeyRecord> list_all() const;

    /// \brief Проверяет доступность MDBX для readiness-проб.
    /// \return `true`, если read-транзакция успешно открывается.
    bool is_healthy() const;

private:
    std::optional<MdbxKeyRecord> get_by_key(const mdbx::txn &txn, mdbx::map_handle map, const std::string &key) const;
    static ApiKeyRecord to_api_key_record(const MdbxKeyRecord &record);
    static bool is_expired(const MdbxKeyRecord &record, std::int64_t now_ms);

    std::string m_env_path;
    mutable std::mutex m_mutex;
    mdbx::env_managed m_env;
    mdbx::map_handle m_dbi_by_id;
    mdbx::map_handle m_dbi_by_fingerprint;
    mdbx::map_handle m_dbi_by_name;
};

} // namespace dfh_node
