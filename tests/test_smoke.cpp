/**
 * \file test_smoke.cpp
 * \brief Smoke-тест версий и идентификаторов ноды.
 * \details Проверяет согласованность API версии.
 */
#include <cassert>

#include "version.hpp"

int main() {
    // Базовая проверка соответствия констант и функций.
    assert(dfh_node::version() == dfh_node::kVersion);
    assert(dfh_node::name() == dfh_node::kNodeName);
    return 0;
}
