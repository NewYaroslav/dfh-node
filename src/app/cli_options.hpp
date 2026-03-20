/// \file cli_options.hpp
/// \brief Разбор аргументов командной строки `dfh_node_app`.
/// \details Изолирует работу с `cxxopts`, чтобы `main` и приложение не
/// содержали низкоуровневого CLI-кода.

#pragma once

#include <filesystem>
#include <string>

namespace dfh_node::app {

/// \brief Нормализованные параметры запуска приложения.
struct CliOptions {
    std::filesystem::path config_path; ///< Путь к JSON-конфигурации ноды.
    bool run_mode{false};              ///< Запускать ли HTTP/WS runtime после bootstrap.
};

/// \brief Результат разбора командной строки.
struct CliParseResult {
    bool ok{false};              ///< Успешен ли разбор аргументов.
    int exit_code{0};            ///< Код возврата для немедленного завершения.
    bool write_to_stderr{false}; ///< Писать ли `message` в `std::cerr`.
    std::string message;         ///< Сообщение об ошибке или help-текст.
    CliOptions options;          ///< Нормализованные параметры при `ok == true`.
};

/// \brief Разбирает аргументы командной строки приложения.
/// \param argc Количество аргументов.
/// \param argv Массив аргументов.
/// \return Нормализованный результат разбора или сообщение для завершения.
CliParseResult parse_cli_options(int argc, char **argv);

} // namespace dfh_node::app
