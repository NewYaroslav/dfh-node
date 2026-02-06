/**
 * \file config_loader.hpp
 * \brief Загрузка конфигурации из JSON-файла.
 * \details Возвращает результат с перечнем ошибок, без исключений парсинга.
 */
#pragma once

#include "config.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dfh_node::config {

/// \brief Описание ошибки загрузки конфигурации.
/// \details path содержит путь к полю в JSON (например, "http.port").
struct LoadError {
    std::string path; ///< JSON-путь до проблемного поля.
    std::string code; ///< Машиночитаемый код ошибки.
    std::string message; ///< Человекочитаемое сообщение.
};

/// \brief Результат загрузки конфигурации из файла.
/// \details При наличии ошибок config может отсутствовать.
struct LoadResult {
    std::optional<Config> config; ///< Загруженная конфигурация или std::nullopt.
    std::vector<LoadError> errors; ///< Список ошибок парсинга/валидации.

    /// \brief Быстрая проверка успешности загрузки.
    /// \return true, если config задана и список ошибок пуст.
    /// \throws Не бросает.
    /// \note Потокобезопасно для чтения.
    bool is_ok() const { return config.has_value() && errors.empty(); }
};

/// \brief Загружает конфигурацию из JSON-файла.
/// \param path Путь к файлу конфигурации.
/// \return Результат загрузки с ошибками или готовой конфигурацией.
/// \throws std::filesystem::filesystem_error при проблемах с доступом к пути.
/// \note Побочные эффекты: чтение файла с диска.
/// \note Потокобезопасно, глобальное состояние не используется.
LoadResult load_from_file(const std::filesystem::path &path);

} // namespace dfh_node::config
