/// \file dfh_node_application.hpp
/// \brief Обёртка приложения `dfh_node_app`.
/// \details Инкапсулирует разбор CLI, bootstrap конфигурации и запуск runtime,
/// чтобы `main.cpp` оставался минимальным.

#pragma once

namespace dfh_node::app {

/// \brief Приложение командной строки для запуска dfh-node.
class DfhNodeApplication {
public:
    /// \brief Создаёт приложение с аргументами процесса.
    /// \param argc Количество аргументов.
    /// \param argv Массив аргументов.
    DfhNodeApplication(int argc, char **argv);

    /// \brief Выполняет разбор CLI, bootstrap и при необходимости runtime.
    /// \return Код завершения процесса.
    int run();

private:
    int m_argc;
    char **m_argv;
};

} // namespace dfh_node::app
