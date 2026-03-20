/// \file cli_options.cpp
/// \brief Реализация разбора аргументов командной строки `dfh_node_app`.
/// \details Использует `cxxopts` и централизует help/error-поведение для
/// точки входа приложения.

#include "cli_options.hpp"

#include <cxxopts.hpp>

#include <sstream>
#include <string>

namespace dfh_node::app {
namespace {

/// \brief Строит объект `cxxopts::Options` с полным описанием CLI.
/// \return Настроенный parser `cxxopts`.
cxxopts::Options make_options() {
    cxxopts::Options options("dfh_node_app", "Bootstrap и runtime ноды dfh-node");
    options.custom_help("--config <path> [--run]");
    options.add_options("Основные параметры")("config", "Путь к JSON-конфигурации", cxxopts::value<std::string>(),
                                              "PATH")(
        "run", "Запустить HTTP и WS runtime",
        cxxopts::value<bool>()->default_value("false")->implicit_value("true"))("h,help", "Показать справку");
    return options;
}

/// \brief Формирует итоговый help-текст для пользователя CLI.
/// \return Полный help-текст `cxxopts`.
std::string build_help_text() { return make_options().help({"Основные параметры"}); }

} // namespace

CliParseResult parse_cli_options(int argc, char **argv) {
    try {
        cxxopts::Options options = make_options();
        const cxxopts::ParseResult result = options.parse(argc, argv);

        if (result.count("help") != 0U) {
            return CliParseResult{false, 0, false, build_help_text(), {}};
        }

        if (result.count("config") == 0U) {
            std::ostringstream stream;
            stream << "Missing required option --config\n\n" << build_help_text();
            return CliParseResult{false, 1, true, stream.str(), {}};
        }

        CliOptions parsed;
        parsed.config_path = result["config"].as<std::string>();
        parsed.run_mode = result["run"].as<bool>();
        return CliParseResult{true, 0, false, {}, parsed};
    } catch (const cxxopts::exceptions::exception &error) {
        std::ostringstream stream;
        stream << "CLI error: " << error.what() << "\n\n" << build_help_text();
        return CliParseResult{false, 1, true, stream.str(), {}};
    }
}

} // namespace dfh_node::app
