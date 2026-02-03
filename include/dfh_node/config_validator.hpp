/**
 * @file config_validator.hpp
 * @brief Проверка целостности и диапазонов параметров конфигурации.
 * @details Валидатор возвращает список ошибок без исключений.
 */
#pragma once

#include "dfh_node/config.hpp"

#include <string>
#include <vector>

namespace dfh_node::config {

/// @brief Описание ошибки валидации конфигурации.
/// @details path содержит путь к полю (например, "logging.level").
struct ValidationError {
    std::string path; ///< Путь к полю конфигурации.
    std::string code; ///< Машиночитаемый код ошибки.
    std::string message; ///< Человекочитаемое сообщение.
};

/// @brief Проверяет конфигурацию на валидность.
/// @param cfg Конфигурация, подлежащая проверке.
/// @return Список ошибок; пустой список означает валидную конфигурацию.
/// @throws Не бросает.
/// @note Потокобезопасно, вход не модифицируется.
std::vector<ValidationError> validate(const Config &cfg);

} // namespace dfh_node::config
