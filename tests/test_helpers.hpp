/// \file test_helpers.hpp
/// \brief Минимальные макросы проверок для unit/smoke тестов.
/// \details Преднамеренно без стороннего фреймворка и с общими тестовыми
/// заглушками.
///
#pragma once

#include "core.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>

// Базовая проверка с завершением процесса при провале.
#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "CHECK FAILED: " << #condition << " at " << __FILE__ << ":" << __LINE__ << "\n";              \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)

// Утилиты для читабельных сравнений.
#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_NE(a, b) CHECK((a) != (b))

/// \brief Тестовые часы с управляемым временем.
class MockClock : public dfh_node::IClock {
public:
    /// \brief Создаёт часы с заданным стартовым временем.
    /// \param initial_ms Начальное значение времени в миллисекундах.
    explicit MockClock(std::uint64_t initial_ms = 0) : m_now_ms(initial_ms) {}

    /// \brief Возвращает текущее тестовое время.
    /// \return Время в миллисекундах.
    std::uint64_t now_ms() const override { return m_now_ms; }

    /// \brief Устанавливает новое тестовое время.
    /// \param ms Новое значение времени в миллисекундах.
    void set_now(std::uint64_t ms) { m_now_ms = ms; }

private:
    std::uint64_t m_now_ms;
};
