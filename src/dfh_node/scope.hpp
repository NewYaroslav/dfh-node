/**
 * \file scope.hpp
 * \brief Определения scope и утилиты авторизации.
 * \details Содержит битмаску Scope, парсинг из конфигурации и маппинг TaskKind в требуемый scope.
 */
#pragma once

#include "task.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace dfh_node {

/// \brief Scope-права для авторизации (битовая маска).
enum class Scope : std::uint8_t {
    None = 0,       ///< Отсутствие прав.
    Read = 1 << 0,  ///< Чтение истории.
    Write = 1 << 1, ///< Запись/ingest.
    Admin = 1 << 2, ///< Административные права.
    Sync = 1 << 3   ///< Межнодовая синхронизация.
};

/// \brief Тип маски для комбинирования scope-прав.
using ScopeMask = std::uint8_t;

/// \brief Объединить два scope в одну маску.
/// \param left Левый scope.
/// \param right Правый scope.
/// \return Маска прав, содержащая оба scope.
constexpr ScopeMask operator|(Scope left, Scope right) {
    return static_cast<ScopeMask>(left) | static_cast<ScopeMask>(right);
}

/// \brief Добавить scope к уже существующей маске.
/// \param mask Текущая маска.
/// \param scope Добавляемый scope.
/// \return Новая маска с добавленным scope.
constexpr ScopeMask operator|(ScopeMask mask, Scope scope) {
    return mask | static_cast<ScopeMask>(scope);
}

/// \brief Проверить наличие требуемого scope в маске.
/// \param mask Маска прав.
/// \param required Требуемый scope.
/// \return true, если scope присутствует или включён Admin override.
inline bool has_scope(ScopeMask mask, Scope required) {
    return (mask & static_cast<ScopeMask>(required)) != 0
        || (mask & static_cast<ScopeMask>(Scope::Admin)) != 0;
}

/// \brief Преобразовать строку scope из конфига в enum.
/// \param value Строковое имя scope ("read", "write", "admin", "sync").
/// \return Scope при валидном значении, иначе std::nullopt.
inline std::optional<Scope> parse_scope(const std::string& value) {
    if (value == "read") {
        return Scope::Read;
    }
    if (value == "write") {
        return Scope::Write;
    }
    if (value == "admin") {
        return Scope::Admin;
    }
    if (value == "sync") {
        return Scope::Sync;
    }
    return std::nullopt;
}

/// \brief Определить обязательный scope для типа задачи.
/// \param kind Тип доменной операции.
/// \return Требуемый scope или std::nullopt для неизвестного TaskKind.
inline std::optional<Scope> required_scope(TaskKind kind) {
    switch (kind) {
    case TaskKind::Ingest:
        return Scope::Write;
    case TaskKind::History:
        return Scope::Read;
    default:
        // Runtime-safe поведение для будущих/неизвестных значений enum.
        return std::nullopt;
    }
}

} // namespace dfh_node
