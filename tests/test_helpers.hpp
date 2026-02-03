/**
 * @file test_helpers.hpp
 * @brief Минимальные макросы проверок для unit/smoke тестов.
 * @details Преднамеренно без стороннего фреймворка.
 */
#pragma once

#include <cstdlib>
#include <iostream>

// Базовая проверка с завершением процесса при провале.
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "CHECK FAILED: " << #condition << " at " << __FILE__  \
                      << ":" << __LINE__ << "\n";                              \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

// Утилиты для читабельных сравнений.
#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_NE(a, b) CHECK((a) != (b))
