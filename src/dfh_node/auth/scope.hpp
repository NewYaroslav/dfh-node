/// \file scope.hpp
/// \brief Определения scope и утилиты авторизации.
/// \details Содержит битмаску Scope, парсинг из конфигурации и маппинг TaskKind
/// в требуемый scope.
///
#pragma once

#include "core/task.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

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

/// \brief Преобразует scope в битовую маску.
/// \param scope Значение scope.
/// \return Битовая маска соответствующего scope.
constexpr ScopeMask to_scope_mask(const Scope scope) { return static_cast<ScopeMask>(scope); }

/// \brief Объединить два scope в одну маску.
/// \param left Левый scope.
/// \param right Правый scope.
/// \return Маска прав, содержащая оба scope.
constexpr ScopeMask operator|(const Scope left, const Scope right) {
    return to_scope_mask(left) | to_scope_mask(right);
}

/// \brief Добавить scope к уже существующей маске.
/// \param mask Текущая маска.
/// \param scope Добавляемый scope.
/// \return Новая маска с добавленным scope.
constexpr ScopeMask operator|(const ScopeMask mask, const Scope scope) { return mask | to_scope_mask(scope); }

/// \brief Проверить наличие требуемого scope в маске.
/// \param mask Маска прав.
/// \param required Требуемый scope.
/// \return true, если scope присутствует или включён Admin override.
inline bool has_scope(const ScopeMask mask, const Scope required) {
    return (mask & to_scope_mask(required)) != 0 || (mask & to_scope_mask(Scope::Admin)) != 0;
}

/// \brief Преобразовать строку scope из конфига в enum.
/// \param value Строковое имя scope ("read", "write", "admin", "sync").
/// \return Scope при валидном значении, иначе std::nullopt.
inline std::optional<Scope> parse_scope(const std::string_view value) {
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
inline std::optional<Scope> required_scope(const TaskKind kind) {
    switch (kind) {
    case TaskKind::Ingest:
        return Scope::Write;
    case TaskKind::History:
        return Scope::Read;
    default:
        // Безопасное поведение в runtime для будущих/неизвестных значений enum.
        return std::nullopt;
    }
}

} // namespace dfh_node
