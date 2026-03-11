/// \file main.cpp
/// \brief Минимальная точка входа `dfh_node_app`.
/// \details Делегирует всю логику объекту `DfhNodeApplication`, чтобы не
/// смешивать CLI, bootstrap и runtime orchestration в `main`.

#include "dfh_node_application.hpp"

int main(int argc, char **argv) { return dfh_node::app::DfhNodeApplication(argc, argv).run(); }
